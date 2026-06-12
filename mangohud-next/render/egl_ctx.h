#pragma once
#pragma once
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "shared.h"
#include "export.h"

class ImGuiEGL;

class EglCtx {
public:
    EGLDisplay dpy = EGL_NO_DISPLAY;
    int renderer = -1;
    std::shared_ptr<ImGuiCtx> imgui;

    explicit EglCtx(int renderer = -1, std::shared_ptr<ImGuiCtx> imgui = nullptr);
    bool init_client(clientRes* r, int buffer_size);
    void destroy_client(clientRes* r);
    int submit(clientRes* r, int idx);

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

    int pick_device();
    bool choose_config(uint32_t format, EGLConfig* out);
    std::vector<uint64_t> get_modifiers(EGLDisplay dpy, uint32_t fourcc);
    bool init_dmabuf(clientRes* r, dmabuf_t& dmabuf);
    void destroy_dmabuf_res(dmabuf_t& dmabuf);
};
