#pragma once
#include <gbm.h>
#include <vulkan/vulkan.h>
#include "../server/config.h"
#include "poll.h"
#include "../server/common/helpers.hpp"
#include "imgui.h"
#include <GL/gl.h>
#include <EGL/egl.h>

enum class Backend : int32_t {
    NONE = 0,
    GLX = 1,
    EGL = 2,
    VULKAN = 3,
};

__attribute__((unused))
static bool sync_fd_signaled(int fd) {
    if (fd < 0) return true;
    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int r = poll(&pfd, 1, 0);
    if (r < 0) {
        return false;
    }
    if (r == 0) {
        return false;
    }
    return (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
}

__attribute__((unused))
static bool sync_fd_blocking(int fd) {
    if (fd < 0)
        return false;

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    for (;;) {
        int r = poll(&pfd, 1, 1000);
        if (r == 1)
            return (pfd.revents & (POLLIN | POLLHUP)) != 0;
        if (r == 0)
            continue;
        if (errno == EINTR)
            continue;

        return false;
    }
}

struct Resolution {
    uint32_t w;
    uint32_t h;

    bool operator==(const Resolution&) const = default;
};

struct ready_frame {
    int idx = -1;
    unique_fd fd;
};

struct gbmBuffer {
    gbm_device* dev = nullptr;
    gbm_bo* bo = nullptr;

    unique_fd fd;
    uint64_t modifier = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;

    uint32_t fourcc = 0;
    uint64_t plane_size = 0;

    gbmBuffer() = default;

    gbmBuffer(const gbmBuffer&) = delete;
    gbmBuffer& operator=(const gbmBuffer&) = delete;

    gbmBuffer(gbmBuffer&& o) noexcept { *this = std::move(o); }

    gbmBuffer& operator=(gbmBuffer&& o) noexcept {
        if (this != &o) {
            if (bo)
                gbm_bo_destroy(bo);
            if (dev)
                gbm_device_destroy(dev);

            dev = o.dev;
            bo = o.bo;
            fd = std::move(o.fd);
            modifier = o.modifier;
            stride = o.stride;
            offset = o.offset;
            fourcc = o.fourcc;
            plane_size = o.plane_size;

            o.dev = nullptr;
            o.bo = nullptr;
        }
        return *this;
    }

    ~gbmBuffer() {
        if (bo)
            gbm_bo_destroy(bo);

        if (dev)
            gbm_device_destroy(dev);

    }
};

class VkCtx;
class EglCtx;
class ImGuiCtx;

struct vk_image_res_t {
    VkImage         image               = VK_NULL_HANDLE;
    VkDeviceMemory  mem                 = VK_NULL_HANDLE;
    VkImageView     view                = VK_NULL_HANDLE;
    VkImageLayout   layout              = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct egl_image_res_t {
    GLuint tex = 0;
    GLuint fbo = 0;
    GLuint mem = 0; // for opaque fd
    EGLImage image = EGL_NO_IMAGE;
};

struct dmabuf_t {
    vk_image_res_t  image_res{};
    egl_image_res_t egl_res{};
    gbmBuffer       gbm{};
};

struct opauqe_t {
    vk_image_res_t  image_res{};
    egl_image_res_t egl_res{};
    VkDeviceSize    size                = 0;
    VkDeviceSize    offset              = 0;
    unique_fd       fd;
};

struct source_t {
    vk_image_res_t  image_res{};
};

struct sync_t {
    VkCommandBuffer         cmd                 = VK_NULL_HANDLE;
    VkFence                 fence               = VK_NULL_HANDLE;
};

struct BufferSet {
    dmabuf_t dmabuf{};
    opauqe_t opaque{};
    source_t source{};
    sync_t sync{};
};

inline void destroy_vk_images(VkDevice device, vk_image_res_t& res) {
    if (res.view) {
        vkDestroyImageView(device, res.view, nullptr);
        res.view = VK_NULL_HANDLE;
    }
    if (res.image) {
        vkDestroyImage(device, res.image, nullptr);
        res.image = VK_NULL_HANDLE;
    }
    if (res.mem) {
        vkFreeMemory(device, res.mem, nullptr);
        res.mem = VK_NULL_HANDLE;
    }

    res.layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

inline const char* fourcc_to_string(uint32_t fourcc)
{
    static thread_local char s[5];

    s[0] = static_cast<char>(fourcc & 0xff);
    s[1] = static_cast<char>((fourcc >> 8) & 0xff);
    s[2] = static_cast<char>((fourcc >> 16) & 0xff);
    s[3] = static_cast<char>((fourcc >> 24) & 0xff);
    s[4] = '\0';

    return s;
}

inline const char* egl_error_string(EGLint err) {
    switch (err) {
        case EGL_SUCCESS:             return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED:     return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:          return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:           return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:       return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONTEXT:         return "EGL_BAD_CONTEXT";
        case EGL_BAD_CONFIG:          return "EGL_BAD_CONFIG";
        case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY:         return "EGL_BAD_DISPLAY";
        case EGL_BAD_SURFACE:         return "EGL_BAD_SURFACE";
        case EGL_BAD_MATCH:           return "EGL_BAD_MATCH";
        case EGL_BAD_PARAMETER:       return "EGL_BAD_PARAMETER";
        case EGL_BAD_NATIVE_PIXMAP:   return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW:   return "EGL_BAD_NATIVE_WINDOW";
        case EGL_CONTEXT_LOST:        return "EGL_CONTEXT_LOST";
        default:                      return "EGL_UNKNOWN_ERROR";
    }
}
