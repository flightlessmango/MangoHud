#include "gl.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <drm/drm_fourcc.h>

GLX::GLX() {
    p_glCreateMemoryObjectsEXT = (PFNGLCREATEMEMORYOBJECTSEXTPROC)glx_gp("glCreateMemoryObjectsEXT");
    p_glDeleteMemoryObjectsEXT = (PFNGLDELETEMEMORYOBJECTSEXTPROC)glx_gp("glDeleteMemoryObjectsEXT");
    p_glImportMemoryFdEXT      = (PFNGLIMPORTMEMORYFDEXTPROC)glx_gp("glImportMemoryFdEXT");
    p_glTexStorageMem2DEXT     = (PFNGLTEXSTORAGEMEM2DEXTPROC)glx_gp("glTexStorageMem2DEXT");
}

bool GLX::import_dmabuf(const Fdinfo& fdinfo, GLuint& tex, GLuint& memobj, int f) {
    if (!p_glCreateMemoryObjectsEXT || !p_glImportMemoryFdEXT || !p_glTexStorageMem2DEXT)
        return false;

    if (memobj) {
        glDeleteMemoryObjectsEXT(1, &memobj);
        memobj = 0;
    }

    p_glCreateMemoryObjectsEXT(1, &memobj);

    int fd = dup(f);
    if (fd < 0)
        return false;

    while (glGetError() != GL_NO_ERROR) {}

    p_glImportMemoryFdEXT(
        memobj,
        fdinfo.plane_size,
        GL_HANDLE_TYPE_OPAQUE_FD_EXT,
        fd
    );

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        close(fd);
        SPDLOG_ERROR("glImportMemoryFdEXT failed: 0x{:x}", err);
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, tex);

    p_glTexStorageMem2DEXT(
        GL_TEXTURE_2D,
        1,
        GL_RGBA8, // try this before SRGB
        fdinfo.w,
        fdinfo.h,
        memobj,
        0
    );

    err = glGetError();
    if (err != GL_NO_ERROR) {
        SPDLOG_ERROR("glTexStorageMem2DEXT failed: 0x{:x}", err);
        return false;
    }

    GLint w = 0;
    GLint h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

    if (w == 0 || h == 0) {
        SPDLOG_ERROR("GL memory object import produced empty texture: {}x{}", w, h);
        return false;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_GREEN);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ALPHA);

    return true;
}

bool GLX::import_opaque_fd(GLuint tex, const Fdinfo& fdinfo, GLuint& memobj, int f) {
    if (!p_glCreateMemoryObjectsEXT || !p_glImportMemoryFdEXT || !p_glTexStorageMem2DEXT) {
        fprintf(stderr, "missing GL_EXT_memory_object_fd procs\n");
        return false;
    }

    drain_gl_errors("import_opaque_fd(entry)");

    if (memobj) {
        glDeleteMemoryObjectsEXT(1, &memobj);
        memobj = 0;
    }

    p_glCreateMemoryObjectsEXT(1, &memobj);
    if (drain_gl_errors("glCreateMemoryObjectsEXT")) return false;

    glBindTexture(GL_TEXTURE_2D, tex);
    if (drain_gl_errors("glBindTexture")) return false;

    int fd = dup(f);
    if (fd < 0) {
        perror("dup(opaque_fd)");
        return false;
    }

    p_glImportMemoryFdEXT(memobj, (GLsizeiptr)fdinfo.opaque_size,
                        GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd);
    if (drain_gl_errors("glImportMemoryFdEXT")) return false;

    const GLenum internalFormat = GL_RGBA8;
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

static bool framebuffer_encodes_srgb(GLint fbo) {
    GLint encoding = GL_LINEAR;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glGetFramebufferAttachmentParameteriv(
        GL_DRAW_FRAMEBUFFER,
        fbo == 0 ? GL_BACK_LEFT : GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING,
        &encoding
    );

    if (glGetError() != GL_NO_ERROR)
        return false;

    return encoding == GL_SRGB;
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
    p_eglGetPlatformDisplayEXT      = (PFNEGLGETPLATFORMDISPLAYEXTPROC)real_eglGetProcAddress("eglGetPlatformDisplayEXT");
    p_eglQueryDmaBufModifiersEXT    = (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)real_eglGetProcAddress("eglQueryDmaBufModifiersEXT");
    p_eglQueryDevicesEXT            = (PFNEGLQUERYDEVICESEXTPROC)real_eglGetProcAddress("eglQueryDevicesEXT");
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

EGLDisplay EGL::import_dmabuf(const Fdinfo& fdinfo, GLuint& tex, EGLImageKHR& image, int f, Display* xdpy) {
    EGLDisplay dpy = eglGetCurrentDisplay();

    // if (dpy == EGL_NO_DISPLAY)
    //     dpy = display_from_device_for_modifier(fdinfo.fourcc, fdinfo.modifier);

    // if (dpy == EGL_NO_DISPLAY && xdpy)
    //     dpy = display_from_glx(xdpy);

    if (dpy == EGL_NO_DISPLAY) return dpy;

    const EGLint img_attrs[] = {
        EGL_WIDTH,  (EGLint)fdinfo.w,
        EGL_HEIGHT, (EGLint)fdinfo.h,
        EGL_LINUX_DRM_FOURCC_EXT, (EGLint)fdinfo.fourcc,
        EGL_DMA_BUF_PLANE0_FD_EXT, f,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)fdinfo.dmabuf_offset,
        EGL_DMA_BUF_PLANE0_PITCH_EXT,  (EGLint)fdinfo.stride,
        EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, (EGLint)(fdinfo.modifier & 0xffffffff),
        EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, (EGLint)(fdinfo.modifier >> 32),
        EGL_NONE
    };
    if (!display_supports_modifier(dpy, DRM_FORMAT_ABGR8888, fdinfo.modifier)) {
        SPDLOG_ERROR(
            "EGLDisplay does not support fourcc=0x{:x}, modifier=0x{:x}",
            fdinfo.fourcc,
            fdinfo.modifier
        );
    }

    image = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                                        (EGLClientBuffer)NULL, img_attrs);
    if (image == EGL_NO_IMAGE_KHR) {
        SPDLOG_ERROR("eglCreateImageKHR failed, EGL error 0x{:x}", eglGetError());
        return EGL_NO_DISPLAY;
    }

    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    p_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)image);

    GLenum err = glGetError();

    GLint w = 0;
    GLint h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

    if (err != GL_NO_ERROR || w == 0 || h == 0) {
        SPDLOG_ERROR(
            "glEGLImageTargetTexture2DOES failed/empty: err=0x{:x}, size={}x{}",
            err,
            w,
            h
        );
        return EGL_NO_DISPLAY;
    }

    return dpy;
}

EGLDisplay EGL::display_from_glx(Display* xdpy) {
    if (!xdpy)
        return EGL_NO_DISPLAY;

    EGLDisplay dpy = EGL_NO_DISPLAY;
    dpy = p_eglGetPlatformDisplayEXT(EGL_PLATFORM_X11_EXT, reinterpret_cast<void*>(xdpy), nullptr);
    if (dpy == EGL_NO_DISPLAY) dpy = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(xdpy));

    if (!eglInitialize(dpy, nullptr, nullptr)) {
        SPDLOG_ERROR("eglInitialize failed: 0x{:x}", eglGetError());
        return EGL_NO_DISPLAY;
    }

    return dpy;
}

EGLDisplay EGL::display_from_device_for_modifier(uint32_t fourcc, uint64_t modifier) {
    if (!p_eglQueryDevicesEXT || !p_eglGetPlatformDisplayEXT)
        return EGL_NO_DISPLAY;

    EGLint count = 0;
    if (!p_eglQueryDevicesEXT(0, nullptr, &count) || count <= 0)
        return EGL_NO_DISPLAY;

    std::vector<EGLDeviceEXT> devices(count);
    if (!p_eglQueryDevicesEXT(count, devices.data(), &count))
        return EGL_NO_DISPLAY;

    for (EGLint i = 0; i < count; i++) {
        EGLDisplay dpy = p_eglGetPlatformDisplayEXT(
            EGL_PLATFORM_DEVICE_EXT,
            devices[i],
            nullptr
        );

        if (dpy == EGL_NO_DISPLAY)
            continue;

        if (!eglInitialize(dpy, nullptr, nullptr))
            continue;

        SPDLOG_INFO(
            "EGLDevice[{}] vendor: {}",
            i,
            eglQueryString(dpy, EGL_VENDOR)
        );

        if (display_supports_modifier(dpy, fourcc, modifier))
            return dpy;

        eglTerminate(dpy);
    }

    return EGL_NO_DISPLAY;
}

// static const char* safe_str(const GLubyte* s) {
//     return s ? reinterpret_cast<const char*>(s) : "(null)";
// }

// static const char* safe_egl_str(const char* s) {
//     return s ? s : "(null)";
// }

bool EGL::display_supports_modifier(EGLDisplay dpy, uint32_t fourcc, uint64_t modifier) {
    // const char* gl_vendor = safe_str(glGetString(GL_VENDOR));
    // const char* gl_renderer = safe_str(glGetString(GL_RENDERER));
    // const char* gl_version = safe_str(glGetString(GL_VERSION));

    // const char* egl_vendor = safe_egl_str(eglQueryString(dpy, EGL_VENDOR));
    // const char* egl_version = safe_egl_str(eglQueryString(dpy, EGL_VERSION));

    // SPDLOG_INFO("GL_VENDOR: {}", gl_vendor);
    // SPDLOG_INFO("GL_RENDERER: {}", gl_renderer);
    // SPDLOG_INFO("GL_VERSION: {}", gl_version);
    // SPDLOG_INFO("EGL_VENDOR: {}", egl_vendor);
    // SPDLOG_INFO("EGL_VERSION: {}", egl_version);

    // const bool gl_nvidia = strstr(gl_vendor, "NVIDIA") || strstr(gl_renderer, "NVIDIA");
    // const bool egl_nvidia = strstr(egl_vendor, "NVIDIA");

    // const bool gl_mesa =
    //     strstr(gl_vendor, "Mesa") ||
    //     strstr(gl_renderer, "Mesa") ||
    //     strstr(gl_version, "Mesa");

    // const bool egl_mesa = strstr(egl_vendor, "Mesa");

    // if (gl_nvidia && egl_mesa) {
    //     SPDLOG_WARN("GLX context appears to be NVIDIA, but fallback EGLDisplay is Mesa. "
    //                 "EGL dma-buf import may reject NVIDIA-supported modifiers.");
    // }

    // if (gl_mesa && egl_nvidia) {
    //     SPDLOG_WARN("GLX context appears to be Mesa, but fallback EGLDisplay is NVIDIA. "
    //                 "EGLImage import/GL consumption may not interop correctly.");
    // }
    if (dpy == EGL_NO_DISPLAY || !p_eglQueryDmaBufModifiersEXT)
        return false;

    EGLint count = 0;
    if (!p_eglQueryDmaBufModifiersEXT(dpy, fourcc, 0, nullptr, nullptr, &count)) {
        SPDLOG_ERROR("eglQueryDmaBufModifiersEXT count failed: 0x{:x}", eglGetError());
        return false;
    }

    if (count <= 0)
        return false;

    std::vector<EGLuint64KHR> modifiers(count);
    std::vector<EGLBoolean> external_only(count);

    if (!p_eglQueryDmaBufModifiersEXT(
            dpy,
            fourcc,
            count,
            modifiers.data(),
            external_only.data(),
            &count)) {
        SPDLOG_ERROR("eglQueryDmaBufModifiersEXT list failed: 0x{:x}", eglGetError());
        return false;
    }

    for (EGLint i = 0; i < count; i++)
        if (static_cast<uint64_t>(modifiers[i]) == modifier)
            return true;

    return false;
}

OverlayGL::OverlayGL(Display* xdpy_, std::shared_ptr<IPCClient> ipc_) : xdpy(xdpy_), ipc(ipc_) {
    SPDLOG_INFO("OpenGL overlay init");
    glx = std::make_unique<GLX>();
    egl = std::make_unique<EGL>();
    auto nodes = find_render_nodes(-1);
    int renderer = -1;
    for (auto node : nodes) {
        renderer = atoi(strrchr(node.c_str(), 'D') + 1);
        SPDLOG_DEBUG("OpenGL renderer: {}", renderer);
    }

    std::string node = *nodes.begin();
    ipc->renderMinor = renderer;
    ipc->pEngineName = pEngineName;
    if (const GLubyte* gpu_name = glGetString(GL_RENDERER))
        ipc->gpuName = clean_gpu_name(reinterpret_cast<const char*>(gpu_name));
    ipc->start(4);
}

CtxRes* OverlayGL::get_ctx() {
    {
        auto r = glx->ctx();
         if (r) {
            if (r->inited)
                return r;

            program(r);
            vao_vbo(r);
            create_cache(r, w, h);
            r->inited = true;
            return r;
        }
    }

    {
        auto r = egl->ctx();
        if (r) {
            if (r->inited)
                return r;

            program(r);
            vao_vbo(r);
            create_cache(r, w, h);
            r->inited = true;
            return r;
        }
    }

    return nullptr;
}

void OverlayGL::draw() {
    if (!ipc->connected.load())
        return;

    GLState s;
    CtxRes* c = nullptr;
    c = get_ctx();
    if (ipc->needs_import.load()) {
        if (!c)
            return;

        {
            std::lock_guard lock(ipc->m);
            fdinfo = std::move(ipc->fdinfo);
            ipc->fdinfo = {};
        }

        if (fdinfo.dmabuf_buffer.empty()) {
            c->dmabufs.clear();
            current_slot = -1;
            inited = false;
            return;
        }

        c->dmabufs.clear();
        for (size_t i = 0; i < fdinfo.dmabuf_buffer.size(); i++) {
            auto buf = std::make_unique<dmabuf>();
            if (!import_dmabuf(buf.get(), fdinfo.dmabuf_buffer[i], fdinfo.opaque_buffer[i])) {
                c->dmabufs.clear();
                fdinfo = {};
                current_slot = -1;
                inited = false;
                ipc->send_import_failed();
                return;
            }
            c->dmabufs.push_back(std::move(buf));
        }
        ipc->needs_import.store(false);
        inited = true;
    }

    if (!inited)
        return;

    if (current_slot == -1)
        current_slot = ipc->next_frame();

    if (current_slot >= 0) {
        const bool dst_encodes_srgb = framebuffer_encodes_srgb(s.saved.fbo);
        glDisable(GL_FRAMEBUFFER_SRGB);
        sample_dmabuf(c, fdinfo, s.saved, dst_encodes_srgb);
        // Do not remove this dma-buf fd dependency yet: GL uses it only to export
        // the release fence when the image itself came from an opaque fd.
        // TODO: split GL release fencing from image transport.
        release_fence(ipc.get(), fdinfo.dmabuf_buffer[current_slot].get());
    }

    if (!c)
        return;

    const int surfW = s.saved.viewport[2];
    const int surfH = s.saved.viewport[3];
    if (surfW <= 0 || surfH <= 0)
        return;

    static int lastSurfW = 0;
    static int lastSurfH = 0;
    if (surfW != lastSurfW || surfH != lastSurfH) {
        ipc->send_resolution(static_cast<uint32_t>(surfW), static_cast<uint32_t>(surfH));
        lastSurfW = surfW;
        lastSurfH = surfH;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, s.saved.fbo);
    glViewport(s.saved.viewport[0], s.saved.viewport[1], s.saved.viewport[2], s.saved.viewport[3]);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(c->prog);
    if (c->uTexLoc >= 0)        glUniform1i(c->uTexLoc, 0);
    if (c->uFlipYLoc >= 0)      glUniform1i(c->uFlipYLoc, 0);
    if (c->uSwapRBLoc >= 0)     glUniform1i(c->uSwapRBLoc, 0);
    if (c->uDecodeSRGBLoc >= 0)  glUniform1i(c->uDecodeSRGBLoc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, c->cache_tex);
    (framebuffer_encodes_srgb(s.saved.fbo) ? glEnable : glDisable)(GL_FRAMEBUFFER_SRGB);
    unsigned drawW = std::min((unsigned)c->cache_w, (unsigned)surfW);
    unsigned drawH = std::min((unsigned)c->cache_h, (unsigned)surfH);

    float ndcW = 2.0f * (float)drawW / (float)surfW;
    float ndcH = 2.0f * (float)drawH / (float)surfH;

    float l = -1.0f;
    float r = -1.0f + ndcW;
    float t =  1.0f;
    float b =  1.0f - ndcH;

    float uMax = (c->cache_w > 0) ? ((float)drawW / (float)c->cache_w) : 1.0f;
    float vMax = (c->cache_h > 0) ? ((float)drawH / (float)c->cache_h) : 1.0f;

    float overlay_verts[] = {
        l, b, 0.0f, 0.0f,
        r, b, uMax, 0.0f,
        l, t, 0.0f, vMax,
        r, t, uMax, vMax,
    };

    glBindVertexArray(c->vao);
    glBindBuffer(GL_ARRAY_BUFFER, c->vbo);
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

void OverlayGL::program(CtxRes* r) {
    if (r->prog) return;

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
    r->prog = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    r->uTexLoc = glGetUniformLocation(r->prog, "uTex");
    r->uFlipYLoc      = glGetUniformLocation(r->prog, "uFlipY");
    r->uDecodeSRGBLoc = glGetUniformLocation(r->prog, "uDecodeSRGB");
    r->uSwapRBLoc     = glGetUniformLocation(r->prog, "uSwapRB");
}

void OverlayGL::create_cache(CtxRes* r, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (r->cache_tex && r->cache_fbo && r->cache_w == w && r->cache_h == h) return;

    r->cache_w = w;
    r->cache_h = h;

    if (!r->cache_tex) glGenTextures(1, &r->cache_tex);
    glBindTexture(GL_TEXTURE_2D, r->cache_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    if (!r->cache_fbo) glGenFramebuffers(1, &r->cache_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, r->cache_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->cache_tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "cache FBO incomplete: 0x%x\n", status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OverlayGL::sample_dmabuf(CtxRes* r, const Fdinfo& fdinfo, const GLState::state& saved, bool framebuffer_encodes_srgb) {
    create_cache(r, (int)fdinfo.w, (int)fdinfo.h);

    glBindFramebuffer(GL_FRAMEBUFFER, r->cache_fbo);
    glViewport(0, 0, (GLsizei)r->cache_w, (GLsizei)r->cache_h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_BLEND);

    glUseProgram(r->prog);
    glUseProgram(r->prog);
    if (r->uTexLoc >= 0) glUniform1i(r->uTexLoc, 0);
    if (r->uFlipYLoc >= 0) glUniform1i(r->uFlipYLoc, 1);
    if (r->uDecodeSRGBLoc >= 0) glUniform1i(r->uDecodeSRGBLoc, framebuffer_encodes_srgb);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->dmabufs[current_slot]->tex);

    // Fullscreen quad into cache
    const float verts[] = {
        -1.f, -1.f, 0.f, 0.f,
        1.f, -1.f, 1.f, 0.f,
        -1.f,  1.f, 0.f, 1.f,
        1.f,  1.f, 1.f, 1.f,
    };

    glBindVertexArray(r->vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glFlush();

    glBindFramebuffer(GL_FRAMEBUFFER, saved.fbo);
    glViewport(saved.viewport[0], saved.viewport[1], saved.viewport[2], saved.viewport[3]);
}

int OverlayGL::release_fence(IPCClient* ipc, int dmabuf_fd, bool write) {
    if (!ipc || dmabuf_fd < 0) return -1;

    glFlush();

    dma_buf_export_sync_file data{};
    data.flags = write ? DMA_BUF_SYNC_WRITE : DMA_BUF_SYNC_READ;
    data.fd = -1;

    if (ioctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &data) != 0) {
        const int err = errno;
        SPDLOG_ERROR("DMA_BUF_IOCTL_EXPORT_SYNC_FILE failed: errno={}", err);
        return -1;
    }

    if (data.fd < 0) {
        SPDLOG_ERROR("DMA_BUF_IOCTL_EXPORT_SYNC_FILE returned invalid fd");
        return -1;
    }

    ipc->frame_ready(current_slot, data.fd);
    current_slot = -1;
    return 0;
}
