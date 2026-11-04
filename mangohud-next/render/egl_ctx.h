#pragma once
#include <cstdint>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "shared.h"
#include "export.h"

class ImGuiEGL;

class EglCtx {
public:
    EGLDisplay dpy = EGL_NO_DISPLAY;
    int64_t renderer = -1;

    explicit EglCtx(int64_t renderer = -1);
    bool init_client(std::vector<BufferSet>& buffers, uint32_t w, uint32_t h, int buffer_size);
    void destroy_client(std::vector<BufferSet>& buffers);
    int submit(std::vector<BufferSet>& buffers, uint32_t w, uint32_t h, Resolution& size,
               int idx, std::shared_ptr<HudConfig> hud, std::mutex& hud_m);

    ~EglCtx();

private:
    gbm_device* gbm_dev = nullptr;
    unique_fd dev_fd;
    EGLConfig config = EGL_NO_CONFIG_KHR;
    EGLContext ctx = EGL_NO_CONTEXT;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC p_glEGLImageTargetTexture2DOES = nullptr;
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC p_eglDupNativeFenceFDANDROID = nullptr;
    PFNEGLDESTROYSYNCKHRPROC p_eglDestroySyncKHR = nullptr;
    PFNEGLCREATESYNCKHRPROC p_eglCreateSyncKHR = nullptr;
    PFNEGLQUERYDMABUFMODIFIERSEXTPROC p_eglQueryDmaBufModifiersEXT = nullptr;
    std::mutex m;
    std::shared_ptr<ImGuiCtx> imgui;

    int pick_device();
    bool choose_config(uint32_t format, EGLConfig* out);
    std::vector<uint64_t> get_modifiers(EGLDisplay dpy, uint32_t fourcc);
    bool init_dmabuf(uint32_t w, uint32_t h, dmabuf_t& dmabuf);
    void destroy_dmabuf_res(dmabuf_t& dmabuf);
};
