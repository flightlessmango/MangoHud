#pragma once
#include <gbm.h>
#include <vulkan/vulkan.h>
#include "../server/config.h"
#include "poll.h"
#include "imgui.h"

static constexpr const char* kBusName = "io.mangohud.socket";
static constexpr const char* kObjPath = "/io/mangohud/socket";
static constexpr const char* kIface   = "io.mangohud.socket1";
static constexpr uint32_t kProtoVersion = 1;

__attribute__((unused))
static bool sync_fd_signaled(int fd) {
    if (fd < 0) return false;
    struct pollfd pfd{ .fd = fd, .events = POLLIN };
    int r = poll(&pfd, 1, 0);
    if (r != 1) return false;
    return (pfd.revents & (POLLIN | POLLHUP)) != 0;
}

__attribute__((unused))
static bool synd_fd_blocking(int fd) {
    if (fd < 0) {
        return false;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    for (;;) {
        int r = poll(&pfd, 1, -1);
        if (r == 1)
            return (pfd.revents & (POLLIN | POLLHUP)) != 0;
        if (r == 0)
            continue;
        if (errno == EINTR)
            continue;

        return false;
    }
}

struct gbmBuffer {
    gbm_device* dev;
    gbm_bo* bo;

    int fd = -1;
    uint64_t modifier = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;

    uint32_t fourcc = 0;
    uint64_t plane_size = 0;
    int64_t renderMinor;
};

class VkCtx;
struct clientRes {
    VkDevice device;

    VkImage         dmabuf              = VK_NULL_HANDLE;
    VkDeviceMemory  dmabuf_mem          = VK_NULL_HANDLE;
    VkImageView     dmabuf_view         = VK_NULL_HANDLE;
    VkImageLayout   dmabuf_layout       = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage         src                 = VK_NULL_HANDLE;
    VkDeviceMemory  src_mem             = VK_NULL_HANDLE;
    VkImageView     src_view            = VK_NULL_HANDLE;
    VkImageLayout   src_layout          = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage         opaque              = VK_NULL_HANDLE;
    VkDeviceMemory  opaque_mem          = VK_NULL_HANDLE;
    VkImageView     opaque_view         = VK_NULL_HANDLE;
    VkImageLayout   opaque_layout       = VK_IMAGE_LAYOUT_UNDEFINED;
    VkDeviceSize    opaque_size         = 0;
    VkDeviceSize    opaque_offset       = 0;
    int             opaque_fd           = -1;

    gbmBuffer       gbm{};
    std::mutex      m;
    std::mutex      table_m;
    uint32_t        server_render_minor = 0;

    std::string     client_id;
    int             acquire_fd          = -1;
    int             release_fd          = -1;
    std::shared_ptr<hudTable> table     = nullptr;
    uint32_t        w                   = 0;
    uint32_t        h                   = 0;
    bool            frame_in_flight     = false;

    bool            send_dmabuf         = false;
    bool            reinit_dmabuf       = false;
    bool            initialized         = false;

    float           fps_limit           = 0;
    bool            initial_fence       = true;

    std::shared_ptr<VkCtx> vk;

    void reinit();
    ~clientRes();
};

__attribute__((unused))
static void destroy_client_res(clientRes* r) {
    std::lock_guard lock(r->m);
    if (r->opaque_fd >= 0){
        close(r->opaque_fd);
        r->opaque_fd = -1;
    }

    if (r->acquire_fd >= 0) {
        close(r->acquire_fd);
        r->acquire_fd = -1;
    }

    if (r->release_fd >= 0) {
        close(r->release_fd);
        r->release_fd = -1;
    }

    if (r->gbm.fd >= 0) {
        close(r->gbm.fd);
        r->gbm.fd = -1;
    }

    if (r->gbm.bo) {
        gbm_bo_destroy(r->gbm.bo);
        r->gbm.bo = nullptr;
    }

    if (r->gbm.dev) {
        gbm_device_destroy(r->gbm.dev);
        r->gbm.dev = nullptr;
    }

    if (!r->device)
        return;

    vkDeviceWaitIdle(r->device);
    if (r->dmabuf_view) {
        vkDestroyImageView(r->device, r->dmabuf_view, nullptr);
        r->dmabuf_view = VK_NULL_HANDLE;
    }
    if (r->dmabuf) {
        vkDestroyImage(r->device, r->dmabuf, nullptr);
        r->dmabuf = VK_NULL_HANDLE;
    }
    if (r->dmabuf_mem) {
        vkFreeMemory(r->device, r->dmabuf_mem, nullptr);
        r->dmabuf_mem = VK_NULL_HANDLE;
    }

    if (r->src_view) {
        vkDestroyImageView(r->device, r->src_view, nullptr);
        r->src_view = VK_NULL_HANDLE;
    }
    if (r->src) {
        vkDestroyImage(r->device, r->src, nullptr);
        r->src = VK_NULL_HANDLE;
    }
    if (r->src_mem) {
        vkFreeMemory(r->device, r->src_mem, nullptr);
        r->src_mem = VK_NULL_HANDLE;
    }

    if (r->opaque_view) {
        vkDestroyImageView(r->device, r->opaque_view, nullptr);
        r->opaque_view = VK_NULL_HANDLE;
    }
    if (r->opaque) {
        vkDestroyImage(r->device, r->opaque, nullptr);
        r->opaque = VK_NULL_HANDLE;
    }
    if (r->opaque_mem) {
        vkFreeMemory(r->device, r->opaque_mem, nullptr);
        r->opaque_mem = VK_NULL_HANDLE;
    }

    r->dmabuf_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    r->src_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    r->opaque_layout = VK_IMAGE_LAYOUT_UNDEFINED;
}
