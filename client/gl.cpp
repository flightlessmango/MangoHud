#include "gl.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>


GLX::GLX() {
    p_glCreateMemoryObjectsEXT = (PFNGLCREATEMEMORYOBJECTSEXTPROC)glx_gp("glCreateMemoryObjectsEXT");
    p_glDeleteMemoryObjectsEXT = (PFNGLDELETEMEMORYOBJECTSEXTPROC)glx_gp("glDeleteMemoryObjectsEXT");
    p_glImportMemoryFdEXT      = (PFNGLIMPORTMEMORYFDEXTPROC)glx_gp("glImportMemoryFdEXT");
    p_glTexStorageMem2DEXT     = (PFNGLTEXSTORAGEMEM2DEXTPROC)glx_gp("glTexStorageMem2DEXT");
}

bool GLX::import_dmabuf(const Fdinfo& fdinfo, GLuint& tex) {
    if (!p_glCreateMemoryObjectsEXT || !p_glImportMemoryFdEXT || !p_glTexStorageMem2DEXT) {
        fprintf(stderr, "GLX dmabuf import procs missing\n");
        return false;
    }

    if (!memobj) p_glCreateMemoryObjectsEXT(1, &memobj);
    glBindTexture(GL_TEXTURE_2D, tex);

    const int fd_for_gl = dup(fdinfo.gbm_fd);
    if (fd_for_gl < 0) return false;

    p_glImportMemoryFdEXT(memobj, fdinfo.plane_size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd_for_gl);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "glImportMemoryFdEXT failed: 0x%x\n", err);
        return false;
    }

    const GLenum internalFormat = GL_SRGB8_ALPHA8;
    p_glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, internalFormat,
                            (GLsizei)fdinfo.w, (GLsizei)fdinfo.h,
                            memobj, (GLuint64)fdinfo.dmabuf_offset);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_GREEN);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ALPHA);

    err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "glTexStorageMem2DEXT failed, GL error 0x%x gbm.fd: %i\n", err, fdinfo.gbm_fd);
        return false;
    }
    return true;
}

bool GLX::import_opaque_fd(GLuint tex, const Fdinfo& fdinfo) {
    if (!p_glCreateMemoryObjectsEXT || !p_glImportMemoryFdEXT || !p_glTexStorageMem2DEXT) {
        fprintf(stderr, "missing GL_EXT_memory_object_fd procs\n");
        return false;
    }

    drain_gl_errors("import_opaque_fd(entry)");

    if (!memobj) p_glCreateMemoryObjectsEXT(1, &memobj);
    if (drain_gl_errors("glCreateMemoryObjectsEXT")) return false;

    glBindTexture(GL_TEXTURE_2D, tex);
    if (drain_gl_errors("glBindTexture")) return false;

    int fd_for_gl = dup(fdinfo.opaque_fd);
    if (fd_for_gl < 0) {
        perror("dup(opaque_fd)");
        return false;
    }

    p_glImportMemoryFdEXT(memobj, (GLsizeiptr)fdinfo.opaque_size,
                        GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd_for_gl);
    if (drain_gl_errors("glImportMemoryFdEXT")) return false;

    const GLenum internalFormat = GL_SRGB8_ALPHA8;
    p_glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, internalFormat,
                        fdinfo.w, fdinfo.h, memobj, (GLuint64)fdinfo.opaque_offset);
    if (drain_gl_errors("glTexStorageMem2DEXT")) return false;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_GREEN);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ALPHA);
    if (drain_gl_errors("glTexParameteri(swizzle)")) return false;

    return true;
}

const char* GLX::gl_err_str(GLenum e) {
    switch (e) {
        case GL_NO_ERROR: return "GL_NO_ERROR";
        case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
        default: return "GL_UNKNOWN_ERROR";
    }
}

bool GLX::drain_gl_errors(const char* where) {
    bool had = false;
    for (;;) {
        GLenum e = glGetError();
        if (e == GL_NO_ERROR) break;
        had = true;
        fprintf(stderr, "GL error at %s: 0x%x (%s)\n", where, e, gl_err_str(e));
    }
    return had;
}

EGL::EGL() {
    auto g_libEGL = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!g_libEGL) {
        fprintf(stderr, "dlopen libEGL failed: %s\n", dlerror());
        return;
    }

    dlerror();
    real_eglGetProcAddress =
        (__eglMustCastToProperFunctionPointerType (*)(const char*))dlsym(g_libEGL, "eglGetProcAddress");
    const char* err = dlerror();
    if (err || !real_eglGetProcAddress) {
        fprintf(stderr, "dlsym eglGetProcAddress failed: %s\n", err ? err : "unknown");
        real_eglGetProcAddress = nullptr;
        return;
    }

    p_eglCreateImageKHR             = (PFNEGLCREATEIMAGEKHRPROC)real_eglGetProcAddress("eglCreateImageKHR");
    p_eglDestroyImageKHR            = (PFNEGLDESTROYIMAGEKHRPROC)real_eglGetProcAddress("eglDestroyImageKHR");
    p_glEGLImageTargetTexture2DOES  = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)real_eglGetProcAddress("glEGLImageTargetTexture2DOES");
    p_eglQueryDeviceStringEXT       = (PFNEGLQUERYDEVICESTRINGEXTPROC)real_eglGetProcAddress("eglQueryDeviceStringEXT");
    p_eglQueryDisplayAttribEXT      = (PFNEGLQUERYDISPLAYATTRIBEXTPROC)real_eglGetProcAddress("eglQueryDisplayAttribEXT");
    p_eglQueryDeviceStringEXT       = (PFNEGLQUERYDEVICESTRINGEXTPROC)real_eglGetProcAddress("eglQueryDeviceStringEXT");
    p_eglQueryDisplayAttribEXT      = (PFNEGLQUERYDISPLAYATTRIBEXTPROC)real_eglGetProcAddress("eglQueryDisplayAttribEXT");
    p_eglQueryDeviceStringEXT       = (PFNEGLQUERYDEVICESTRINGEXTPROC)real_eglGetProcAddress("eglQueryDeviceStringEXT");
}

int64_t EGL::renderer() {
    EGLDisplay dpy = eglGetCurrentDisplay();
    if (dpy == EGL_NO_DISPLAY)
        return 1;


    if (!eglInitialize(dpy, NULL, NULL)) {
        printf("eglInitialize failed, err=0x%04x\n", eglGetError());
        return 1;
    }

    EGLAttrib dev_attrib = 0;
    if (!p_eglQueryDisplayAttribEXT(dpy, EGL_DEVICE_EXT, &dev_attrib)) {
        printf("eglQueryDisplayAttribEXT(EGL_DEVICE_EXT) failed, err=0x%04x\n", eglGetError());
        return 1;
    }

    EGLDeviceEXT dev = (EGLDeviceEXT)dev_attrib;
    const char *render = p_eglQueryDeviceStringEXT(dev, EGL_DRM_RENDER_NODE_FILE_EXT);
    return atoi(strrchr(render, 'D') + 1);
}

void EGL::import_dmabuf(const Fdinfo& fdinfo, GLuint& tex) {
    if (renderer() != fdinfo.server_render_minor)
        printf("this is not on the same GPU, bail.");

    EGLDisplay dpy = eglGetCurrentDisplay();
    if (dpy == EGL_NO_DISPLAY) return;

    const EGLint img_attrs[] = {
        EGL_WIDTH,  (EGLint)fdinfo.w,
        EGL_HEIGHT, (EGLint)fdinfo.h,
        EGL_LINUX_DRM_FOURCC_EXT, (EGLint)fdinfo.fourcc,
        EGL_DMA_BUF_PLANE0_FD_EXT, fdinfo.gbm_fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)fdinfo.dmabuf_offset,
        EGL_DMA_BUF_PLANE0_PITCH_EXT,  (EGLint)fdinfo.stride,
        EGL_NONE
    };

    EGLImageKHR img = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                                        (EGLClientBuffer)NULL, img_attrs);
    if (img == EGL_NO_IMAGE_KHR) {
        EGLint err = eglGetError();
        fprintf(stderr, "eglCreateImageKHR failed, EGL error 0x%x\n", err);
        return;
    }

    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    p_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "glEGLImageTargetTexture2DOES failed, GL error 0x%x\n", err);
        return;
    }
}

OverlayGL::OverlayGL(Display* xdpy_) : xdpy(xdpy_) {
    SPDLOG_INFO("OpenGL overlay init");
    glx = std::make_unique<GLX>();
    egl = std::make_unique<EGL>();
    auto nodes = find_render_nodes(-1);
    for (auto node : nodes)
        printf("node: %i\n", atoi(strrchr(node.c_str(), 'D') + 1));

    std::string node = *nodes.begin();
    ipc = std::make_unique<IPCClient>(atoi(strrchr(node.c_str(), 'D') + 1), "");
    ipc->pEngineName = "OpenGL";
}

void OverlayGL::init(CtxRes& r) {
    bind_texture(r);
    if (glXGetCurrentContext()) {
        if (!glx->import_dmabuf(fdinfo, r.tex))
            glx->import_opaque_fd(r.tex, fdinfo);
    }

    if (eglGetCurrentContext() != EGL_NO_CONTEXT)
        egl->import_dmabuf(fdinfo, r.tex);

    program(r);
    vao_vbo(r);
    create_cache(r, (int)fdinfo.w, (int)fdinfo.h);
}

void OverlayGL::draw() {
    GLState s;
    {
        std::lock_guard lock(ipc->m);
        fdinfo = ipc->fdinfo;
    }

    bool have_new = false;
    int acquire_fd = ipc->ready_frame();
    if (acquire_fd >= 0 && (fdinfo.gbm_fd >= 0 || fdinfo.opaque_fd >= 0)) {
        have_new = true;
        close(acquire_fd);
    }

    auto& c = glx->ctx();
    if (!c.inited) {
        if (!have_new)
            return;

        init(c);
        c.inited = true;
    }

    if (have_new) {
        glDisable(GL_FRAMEBUFFER_SRGB);
        sample_dmabuf(c, fdinfo, s.saved);
        release_fence(ipc.get(), fdinfo.gbm_fd);
    }

    const int surfW = s.saved.viewport[2];
    const int surfH = s.saved.viewport[3];
    if (surfW <= 0 || surfH <= 0)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, s.saved.fbo);
    glViewport(s.saved.viewport[0], s.saved.viewport[1], s.saved.viewport[2], s.saved.viewport[3]);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(c.prog);
    if (c.uTexLoc >= 0)        glUniform1i(c.uTexLoc, 0);
    if (c.uFlipYLoc >= 0)      glUniform1i(c.uFlipYLoc, 0);
    if (c.uSwapRBLoc >= 0)     glUniform1i(c.uSwapRBLoc, 0);
    if (c.uDecodeSRGBLoc >= 0)  glUniform1i(c.uDecodeSRGBLoc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, c.cache_tex);
    glEnable(GL_FRAMEBUFFER_SRGB);
    unsigned drawW = std::min((unsigned)c.cache_w, (unsigned)surfW);
    unsigned drawH = std::min((unsigned)c.cache_h, (unsigned)surfH);

    float ndcW = 2.0f * (float)drawW / (float)surfW;
    float ndcH = 2.0f * (float)drawH / (float)surfH;

    float l = -1.0f;
    float r = -1.0f + ndcW;
    float t =  1.0f;
    float b =  1.0f - ndcH;

    float uMax = (c.cache_w > 0) ? ((float)drawW / (float)c.cache_w) : 1.0f;
    float vMax = (c.cache_h > 0) ? ((float)drawH / (float)c.cache_h) : 1.0f;

    float overlay_verts[] = {
        l, b, 0.0f, 0.0f,
        r, b, uMax, 0.0f,
        l, t, 0.0f, vMax,
        r, t, uMax, vMax,
    };

    glBindVertexArray(c.vao);
    glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)sizeof(overlay_verts), overlay_verts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_FRAMEBUFFER_SRGB);
}

GLuint OverlayGL::compile_shader(GLenum type, const char *src) {
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

GLuint OverlayGL::link_program(GLuint vs, GLuint fs) {
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

void OverlayGL::program(CtxRes& r) {
    if (r.prog) return;

    const char *vs_src =
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "out vec2 vUV;\n"
        "void main(){\n"
        "  vUV = aUV;\n"
        "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";

    const char *fs_src =
        "#version 330 core\n"
        "in vec2 vUV;\n"
        "uniform sampler2D uTex;\n"
        "uniform int uFlipY;\n"
        "uniform int uDecodeSRGB;\n"
        "out vec4 fragColor;\n"
        "\n"
        "float srgb_to_linear_1(float x) {\n"
        "  return (x <= 0.04045) ? (x / 12.92) : pow((x + 0.055) / 1.055, 2.4);\n"
        "}\n"
        "vec3 srgb_to_linear(vec3 c) {\n"
        "  return vec3(srgb_to_linear_1(c.r), srgb_to_linear_1(c.g), srgb_to_linear_1(c.b));\n"
        "}\n"
        "\n"
        "void main(){\n"
        "  vec2 uv = vUV;\n"
        "  if (uFlipY != 0) uv.y = 1.0 - uv.y;\n"
        "  vec4 t = texture(uTex, uv);\n"
        "  if (uDecodeSRGB != 0) t.rgb = srgb_to_linear(t.rgb);\n"
        "  fragColor = t;\n"
        "}\n";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    r.prog = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    r.uTexLoc = glGetUniformLocation(r.prog, "uTex");
    r.uFlipYLoc      = glGetUniformLocation(r.prog, "uFlipY");
    r.uDecodeSRGBLoc = glGetUniformLocation(r.prog, "uDecodeSRGB");
    r.uSwapRBLoc     = glGetUniformLocation(r.prog, "uSwapRB");
}

void OverlayGL::bind_texture(CtxRes& r) {
    if (!r.tex) glGenTextures(1, &r.tex);

    glBindTexture(GL_TEXTURE_2D, r.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void OverlayGL::create_cache(CtxRes& r, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (r.cache_tex && r.cache_fbo && r.cache_w == w && r.cache_h == h) return;

    r.cache_w = w;
    r.cache_h = h;

    if (!r.cache_tex) glGenTextures(1, &r.cache_tex);
    glBindTexture(GL_TEXTURE_2D, r.cache_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (!r.cache_fbo) glGenFramebuffers(1, &r.cache_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r.cache_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.cache_tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "cache FBO incomplete: 0x%x\n", status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OverlayGL::sample_dmabuf(CtxRes& r, const Fdinfo& fdinfo, const GLState::state& saved) {
    create_cache(r, (int)fdinfo.w, (int)fdinfo.h);

    glBindFramebuffer(GL_FRAMEBUFFER, r.cache_fbo);
    glViewport(0, 0, (GLsizei)r.cache_w, (GLsizei)r.cache_h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_BLEND);

    glUseProgram(r.prog);
    glUseProgram(r.prog);
    if (r.uTexLoc >= 0) glUniform1i(r.uTexLoc, 0);
    if (r.uFlipYLoc >= 0) glUniform1i(r.uFlipYLoc, 1);
    if (r.uDecodeSRGBLoc >= 0) glUniform1i(r.uDecodeSRGBLoc, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r.tex);

    // Fullscreen quad into cache
    const float verts[] = {
        -1.f, -1.f, 0.f, 0.f,
        1.f, -1.f, 1.f, 0.f,
        -1.f,  1.f, 0.f, 1.f,
        1.f,  1.f, 1.f, 1.f,
    };

    glBindVertexArray(r.vao);
    glBindBuffer(GL_ARRAY_BUFFER, r.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glFlush();

    glBindFramebuffer(GL_FRAMEBUFFER, saved.fbo);
    glViewport(saved.viewport[0], saved.viewport[1], saved.viewport[2], saved.viewport[3]);
}

void OverlayGL::draw_dmabuf(CtxRes& r) {
    if (!r.cache_tex) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(r.prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r.cache_tex);
    if (r.uTexLoc >= 0) glUniform1i(r.uTexLoc, 0);

    glBindVertexArray(r.vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

int OverlayGL::release_fence(IPCClient* ipc, int dmabuf_fd, bool write) {
    if (!ipc || dmabuf_fd < 0) return -1;

    glFlush();

    dma_buf_export_sync_file data{};
    data.flags = write ? DMA_BUF_SYNC_WRITE : DMA_BUF_SYNC_READ;
    data.fd = -1;

    if (ioctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &data) != 0) {
        const int err = errno;
        fprintf(stderr, "DMA_BUF_IOCTL_EXPORT_SYNC_FILE failed: errno=%d\n", err);
        return -1;
    }

    if (data.fd < 0) {
        fprintf(stderr, "DMA_BUF_IOCTL_EXPORT_SYNC_FILE returned invalid fd\n");
        return -1;
    }

    ipc->queue_fence(data.fd);
    return 0;
}
