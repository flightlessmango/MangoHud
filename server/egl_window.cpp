#include "egl_window.h"

static void *must_egl_proc(const char *name) {
  void *p = (void *)eglGetProcAddress(name);
  if (!p) {
    fprintf(stderr, "missing proc: %s\n", name);
    exit(1);
  }
  return p;
}

static GLuint compile_shader(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    GLsizei n = 0;
    glGetShaderInfoLog(s, (GLsizei)sizeof(log), &n, log);
    fprintf(stderr, "shader compile failed:\n%.*s\n", (int)n, log);
    exit(1);
  }
  return s;
}

static GLuint link_program(GLuint vs, GLuint fs) {
  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glBindAttribLocation(p, 0, "aPos");
  glBindAttribLocation(p, 1, "aUV");
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    GLsizei n = 0;
    glGetProgramInfoLog(p, (GLsizei)sizeof(log), &n, log);
    fprintf(stderr, "program link failed:\n%.*s\n", (int)n, log);
    exit(1);
  }
  return p;
}

#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID ((1ULL<<56) - 1)
#endif

void EGLWindow::run() {
  glfwSetErrorCallback([](int code, const char *desc) {
    fprintf(stderr, "GLFW error %d: %s\n", code, desc);
  });

  if (!glfwInit()) printf("glfwInit failed\n");

  // Force EGL context creation, and use OpenGL ES 2 for EGLImage path.
  glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

  GLFWwindow *win = glfwCreateWindow(1280, 720, "Wayland EGL dmabuf overlay", NULL, NULL);
  if (!win) printf("glfwCreateWindow failed\n");

  glfwMakeContextCurrent(win);
  glfwSwapInterval(1);

  // Required procs
  PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR =
      (PFNEGLCREATEIMAGEKHRPROC)must_egl_proc("eglCreateImageKHR");
  PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR =
      (PFNEGLDESTROYIMAGEKHRPROC)must_egl_proc("eglDestroyImageKHR");
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES =
      (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)must_egl_proc("glEGLImageTargetTexture2DOES");

  EGLDisplay dpy = eglGetCurrentDisplay();
  if (dpy == EGL_NO_DISPLAY) printf("no current EGLDisplay (GLFW did not create EGL?)\n");

  // Import the dma-buf fd as EGLImage
  const EGLint img_attrs[] = {
      EGL_WIDTH, (EGLint)gbm.width,
      EGL_HEIGHT, (EGLint)gbm.height,
      EGL_LINUX_DRM_FOURCC_EXT, (EGLint)gbm.fourcc,
      EGL_DMA_BUF_PLANE0_FD_EXT, gbm.fd,
      EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)gbm.offset,
      EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)gbm.stride,
      EGL_NONE
  };

  EGLImageKHR img = eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                                     (EGLClientBuffer)NULL, img_attrs);
  if (img == EGL_NO_IMAGE_KHR) {
    EGLint err = eglGetError();
    fprintf(stderr, "eglCreateImageKHR failed, EGL error 0x%x\n", err);
    printf("dma-buf import failed (need EGL_EXT_image_dma_buf_import + matching metadata)\n");
  }

  // Bind EGLImage to GL texture
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);
  if (glGetError() != GL_NO_ERROR) printf("glEGLImageTargetTexture2DOES failed\n");

  const char *vs_src =
    "attribute vec2 aPos;\n"
    "void main(){ gl_Position = vec4(aPos, 0.0, 1.0); }\n";

  const char *fs_src =
    "precision mediump float;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uTexSize;\n"
    "uniform vec2 uOrigin;\n"        // top-left in pixels
    "uniform vec2 uViewportSize;\n"
    "void main(){\n"
    "  vec2 frag = gl_FragCoord.xy;\n"
    "  vec2 originBL = vec2(uOrigin.x, uViewportSize.y - uOrigin.y - uTexSize.y);\n"
    "  vec2 p = frag - originBL;\n"
    "  if (p.x < 0.0 || p.y < 0.0 || p.x >= uTexSize.x || p.y >= uTexSize.y) discard;\n"
    "  vec2 uv = (p + vec2(0.5)) / uTexSize;\n"
    "  uv.y = 1.0 - uv.y;\n"
    "  gl_FragColor = texture2D(uTex, uv);\n"
    "}\n";

  GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
  GLuint prog = link_program(vs, fs);
  glDeleteShader(vs);
  glDeleteShader(fs);

  glUseProgram(prog);
  GLint uTexLoc          = glGetUniformLocation(prog, "uTex");
  GLint uTexSizeLoc      = glGetUniformLocation(prog, "uTexSize");
  GLint uOriginLoc       = glGetUniformLocation(prog, "uOrigin");
  GLint uViewportSizeLoc = glGetUniformLocation(prog, "uViewportSize");

  glUniform1i(uTexLoc, 0);

  // Set texture sampling once (after binding the texture)
  glBindTexture(GL_TEXTURE_2D, tex);

  while (!glfwWindowShouldClose(win)) {
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(win, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.08f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(prog);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(uTexLoc, 0);

    glUniform2f(uTexSizeLoc, (float)gbm.width, (float)gbm.height);
    glUniform2f(uViewportSizeLoc, (float)fbw, (float)fbh);

    // place at top-left of window
    glUniform2f(uOriginLoc, 0.0f, 0.0f);

    static const float pos[] = {
      -1, -1,
      1, -1,
      -1,  1,
      1,  1
    };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), pos);
    glEnableVertexAttribArray(0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glfwSwapBuffers(win);
    glfwPollEvents();
  }


  glDeleteProgram(prog);
  glDeleteTextures(1, &tex);
  eglDestroyImageKHR(dpy, img);

  glfwDestroyWindow(win);
  glfwTerminate();
}
