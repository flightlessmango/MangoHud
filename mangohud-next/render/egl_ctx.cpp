
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <string>
#include <stdexcept>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <spdlog/spdlog.h>
#include <drm/drm_fourcc.h>
#include <algorithm>

#include "../server/common/helpers.hpp"
#include "egl_ctx.h"
#include "imgui/egl.h"

EglCtx::EglCtx(int renderer_, std::shared_ptr<ImGuiCtx> imgui) : renderer(renderer_), imgui(imgui) {
    p_glEGLImageTargetTexture2DOES =
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
            eglGetProcAddress("glEGLImageTargetTexture2DOES"));

    p_eglDupNativeFenceFDANDROID =
        reinterpret_cast<PFNEGLDUPNATIVEFENCEFDANDROIDPROC>(
            eglGetProcAddress("eglDupNativeFenceFDANDROID"));

    p_eglDestroySyncKHR =
        reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(
            eglGetProcAddress("eglDestroySyncKHR"));

    p_eglCreateSyncKHR =
        reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(
            eglGetProcAddress("eglCreateSyncKHR"));

    p_eglQueryDmaBufModifiersEXT =
        reinterpret_cast<PFNEGLQUERYDMABUFMODIFIERSEXTPROC>(
            eglGetProcAddress("eglQueryDmaBufModifiersEXT"));

    {
        std::lock_guard lock(m);
        if (!dev_fd) dev_fd = unique_fd::adopt(pick_device());
    }

    gbm_dev = gbm_create_device(dev_fd.get());
    if (!gbm_dev) {
        SPDLOG_ERROR("gbm_create_device failed");
        return;
    }

    dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm_dev, nullptr);
    if (dpy == EGL_NO_DISPLAY) {
        SPDLOG_ERROR("eglGetPlatformDisplay failed");
        return;
    }

    EGLint major = 0;
    EGLint minor = 0;

    if (!eglInitialize(dpy, &major, &minor)) {
        SPDLOG_ERROR("eglInitialize failed, err=0x{:x} {}", eglGetError(), egl_error_string(eglGetError()));
        return;
    }

    if (config == EGL_NO_CONFIG_KHR)
        choose_config(DRM_FORMAT_ARGB8888, &config);

    const EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLint n = 0;
    if (!eglChooseConfig(dpy, attrs, &config, 1, &n) || n < 1) {
        EGLint err = eglGetError();
        SPDLOG_ERROR("eglChooseConfig failed, err=0x{:x} {}", err, egl_error_string(err));
        return;
    }

    const EGLint ctx_attribs[] = {
        EGL_NONE
    };

    if (!eglBindAPI(EGL_OPENGL_API)) {
        EGLint err = eglGetError();
        SPDLOG_ERROR("eglBindAPI failed, err=0x{:x} {}", err, egl_error_string(err));
        return;
    }

    ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx == EGL_NO_CONTEXT) {
        EGLint err = eglGetError();
        SPDLOG_ERROR("eglCreateContext failed, err=0x{:x} {}",err, egl_error_string(err));
        return;
    }

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

bool EglCtx::init_client(clientRes* r, int buffer_size) {
    std::lock_guard lock(m);
    if (dpy == EGL_NO_DISPLAY || ctx == EGL_NO_CONTEXT) {
        SPDLOG_ERROR("EGL context is not initialized");
        return false;
    }

    if (!p_glEGLImageTargetTexture2DOES) {
        SPDLOG_ERROR("glEGLImageTargetTexture2DOES is unavailable");
        return false;
    }

    r->buffer.resize(buffer_size);
    for (auto& buf : r->buffer) {
        dmabuf_t& dmabuf = buf.dmabuf;
        if (!dev_fd) dev_fd = unique_fd::adopt(pick_device());

        if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
            EGLint err = eglGetError();
            SPDLOG_ERROR("eglMakeCurrent failed, err=0x{:x} {} dpy: {} ctx: {}", err, egl_error_string(err), dpy, ctx);
            eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            return false;
        }

        if (!init_dmabuf(r, dmabuf)) {
            eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            return false;
        }

        if (imgui && !imgui->egl)
            imgui->init_egl();

        glGenTextures(1, &dmabuf.egl_res.tex);

        glBindTexture(GL_TEXTURE_2D, dmabuf.egl_res.tex);
        p_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)dmabuf.egl_res.image);
        GLenum glerr = glGetError();
        if (glerr != GL_NO_ERROR)
            SPDLOG_ERROR("glEGLImageTargetTexture2DOES failed, glerr=0x{:x}", glerr);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &dmabuf.egl_res.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, dmabuf.egl_res.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dmabuf.egl_res.tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    r->initialized = true;
    r->send_dmabuf = true;
    return true;
}

void EglCtx::destroy_client(clientRes* r) {
    std::lock_guard lock(m);

    if (dpy == EGL_NO_DISPLAY || ctx == EGL_NO_CONTEXT)
        return;

    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        EGLint err = eglGetError();
        SPDLOG_ERROR("eglMakeCurrent failed, err=0x{:x} {} dpy: {} ctx: {}", err, egl_error_string(err), dpy, ctx);
        return;
    }

    for (auto& buf : r->buffer)
        destroy_dmabuf_res(buf.dmabuf);

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

int EglCtx::submit(clientRes* r, int idx) {
    std::lock_guard lock(m);

    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        EGLint err = eglGetError();
        SPDLOG_ERROR("eglMakeCurrent failed, err=0x{:x} {} dpy: {} ctx: {}", err, egl_error_string(err), dpy, ctx);
        return -1;
    }

    GLuint intermediate_tex = 0;
    GLuint intermediate_fbo = 0;

    glGenTextures(1, &intermediate_tex);
    glBindTexture(GL_TEXTURE_2D, intermediate_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, r->w, r->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &intermediate_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, intermediate_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, intermediate_tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        SPDLOG_ERROR("intermediate FBO incomplete: 0x{:x}", status);
        glDeleteFramebuffers(1, &intermediate_fbo);
        glDeleteTextures(1, &intermediate_tex);
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        return -1;
    }

    auto* buf = &r->buffer[idx];

    glBindFramebuffer(GL_FRAMEBUFFER, intermediate_fbo);
    glViewport(0, 0, r->w, r->h);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DITHER);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, r->w, r->h);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!imgui->draw(r, buf, Backend::EGL)) {
        glDeleteFramebuffers(1, &intermediate_fbo);
        glDeleteTextures(1, &intermediate_tex);
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        return -1;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, intermediate_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, buf->dmabuf.egl_res.fbo);

    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(
        0, 0, r->w, r->h,
        0, r->h, r->w, 0,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST
    );

    glDeleteFramebuffers(1, &intermediate_fbo);
    glDeleteTextures(1, &intermediate_tex);

    EGLSyncKHR sync = p_eglCreateSyncKHR(dpy, EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
    if (sync == EGL_NO_SYNC_KHR) {
        EGLint err = eglGetError();
        SPDLOG_ERROR("eglCreateSyncKHR failed, err=0x{:x} {}", err, egl_error_string(err));
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        return -1;
    }

    glFlush();

    int fence_fd = p_eglDupNativeFenceFDANDROID(dpy, sync);
    if (fence_fd < 0) {
        EGLint err = eglGetError();
        SPDLOG_ERROR("eglDupNativeFenceFDANDROID failed, err=0x{:x} {}", err, egl_error_string(err));
        p_eglDestroySyncKHR(dpy, sync);
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        return -1;
    }

    p_eglDestroySyncKHR(dpy, sync);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return fence_fd;
}

int EglCtx::pick_device() {
    drmDevicePtr devices[16] = {};
    int count = drmGetDevices2(0, devices, 16);
    if (count < 0) {
        SPDLOG_ERROR("drmGetDevices2 failed");
        return -1;
    }

    int fd = -1;

    for (int i = 0; i < count; i++) {
        drmDevicePtr dev = devices[i];
        if (!dev)
            continue;

        if (!(dev->available_nodes & (1 << DRM_NODE_RENDER)))
            continue;

        int minor = -1;
        if (const char* render = strrchr(dev->nodes[DRM_NODE_RENDER], 'D'))
            minor = atoi(render + 1);

        if (renderer >= 0 && minor != renderer)
            continue;

        fd = open(dev->nodes[DRM_NODE_RENDER], O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            renderer = minor;
            SPDLOG_DEBUG("picked render {}", dev->nodes[DRM_NODE_RENDER]);
            break;
        }
    }

    drmFreeDevices(devices, count);
    return fd;
}

bool EglCtx::choose_config(uint32_t format, EGLConfig* out) {
    const EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };

    EGLint count = 0;
    if (!eglGetConfigs(dpy, nullptr, 0, &count) || count <= 0) {
        EGLint err = eglGetError();
        SPDLOG_ERROR("eglGetConfigs failed, err=0x{:x} {}", err, egl_error_string(err));
        return false;
    }

    std::vector<EGLConfig> configs(count);
    if (!eglChooseConfig(dpy, attrs, configs.data(), count, &count) || count <= 0) {
        EGLint err = eglGetError();
        SPDLOG_ERROR("eglChooseConfig failed, err=0x{:x} {}", err, egl_error_string(err));
        return false;
    }

    for (int i = 0; i < count; i++) {
        EGLint visual_id = 0;
        if (!eglGetConfigAttrib(dpy, configs[i], EGL_NATIVE_VISUAL_ID, &visual_id))
            continue;

        if (static_cast<uint32_t>(visual_id) == format) {
            *out = configs[i];
            return true;
        }
    }

    SPDLOG_ERROR("no config matched format 0x{:x}", format);
    return false;
}

std::vector<uint64_t> EglCtx::get_modifiers(EGLDisplay dpy, uint32_t fourcc) {
    std::vector<uint64_t> ret;
    if (!p_eglQueryDmaBufModifiersEXT)
        return ret;

    EGLint count = 0;
    if (!p_eglQueryDmaBufModifiersEXT(dpy, fourcc, 0, nullptr, nullptr, &count)) {
        EGLint err = eglGetError();
        SPDLOG_ERROR("eglQueryDmaBufModifiersEXT count failed, err=0x{:x} {}", err, egl_error_string(err));
        return ret;
    }

    if (count <= 0)
        return ret;

    std::vector<EGLuint64KHR> modifiers(count);
    std::vector<EGLBoolean> external_only(count);
    if (!p_eglQueryDmaBufModifiersEXT(
            dpy,
            fourcc,
            count,
            modifiers.data(),
            external_only.data(),
            &count)) {
        EGLint err = eglGetError();
        SPDLOG_ERROR("eglQueryDmaBufModifiersEXT list failed, err=0x{:x} {}", err, egl_error_string(err));
        return ret;
    }

    ret.reserve(count);
    for (EGLint i = 0; i < count; i++)
        ret.push_back(static_cast<uint64_t>(modifiers[i]));

    auto linear = std::find(ret.begin(), ret.end(), DRM_FORMAT_MOD_LINEAR);
    if (linear != ret.end())
        std::rotate(ret.begin(), linear, linear + 1);

    return ret;
}

void EglCtx::destroy_dmabuf_res(dmabuf_t& dmabuf) {
    if (dmabuf.egl_res.fbo) {
        glDeleteFramebuffers(1, &dmabuf.egl_res.fbo);
        dmabuf.egl_res.fbo = 0;
    }

    if (dmabuf.egl_res.tex) {
        glDeleteTextures(1, &dmabuf.egl_res.tex);
        dmabuf.egl_res.tex = 0;
    }

    if (dmabuf.egl_res.image != EGL_NO_IMAGE) {
        eglDestroyImage(dpy, dmabuf.egl_res.image);
        dmabuf.egl_res.image = EGL_NO_IMAGE;
    }

    dmabuf.gbm = {};
}

bool EglCtx::init_dmabuf(clientRes* r, dmabuf_t& dmabuf) {
    constexpr uint32_t fourcc = DRM_FORMAT_ARGB8888;
    auto modifiers = get_modifiers(dpy, fourcc);
    if (modifiers.empty())
        modifiers.push_back(DRM_FORMAT_MOD_LINEAR);

    for (uint64_t modifier : modifiers) {
        destroy_dmabuf_res(dmabuf);

        if (!create_gbm(r, &dmabuf, dev_fd.get(), modifier))
            continue;

        const EGLAttrib img_attrs[] = {
            EGL_WIDTH, EGLint(r->w),
            EGL_HEIGHT, EGLint(r->h),
            EGL_LINUX_DRM_FOURCC_EXT, EGLint(dmabuf.gbm.fourcc),
            EGL_DMA_BUF_PLANE0_FD_EXT, dmabuf.gbm.fd.get(),
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGLint(dmabuf.gbm.offset),
            EGL_DMA_BUF_PLANE0_PITCH_EXT, EGLint(dmabuf.gbm.stride),
            EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGLint(dmabuf.gbm.modifier & 0xffffffffull),
            EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGLint(dmabuf.gbm.modifier >> 32),
            EGL_NONE
        };

        dmabuf.egl_res.image = eglCreateImage(
            dpy,
            EGL_NO_CONTEXT,
            EGL_LINUX_DMA_BUF_EXT,
            nullptr,
            img_attrs
        );

        if (dmabuf.egl_res.image != EGL_NO_IMAGE_KHR) {
            SPDLOG_DEBUG("selected EGL dmabuf modifier=0x{:016x}", modifier);
            return true;
        }

        EGLint err = eglGetError();
        SPDLOG_DEBUG(
            "eglCreateImage failed for modifier=0x{:016x}, err=0x{:x} {}",
            modifier,
            err,
            egl_error_string(err)
        );
    }

    SPDLOG_ERROR("failed to create EGL dmabuf for any advertised modifier");
    destroy_dmabuf_res(dmabuf);
    return false;
}

EglCtx::~EglCtx() {
    if (dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
        eglDestroyContext(dpy, ctx);
        eglTerminate(dpy);
    }

    if (gbm_dev)
        gbm_device_destroy(gbm_dev);
}
