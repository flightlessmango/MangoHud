#pragma once
#include <gbm.h>
#include <vulkan/vulkan.h>
#include "../server/config.h"
#include "poll.h"
#include "../server/common/helpers.hpp"
#include "imgui.h"

static constexpr const char* kBusName = "io.mangohud.socket";
static constexpr const char* kObjPath = "/io/mangohud/socket";
static constexpr const char* kIface   = "io.mangohud.socket1";
static constexpr uint32_t kProtoVersion = 1;

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
        // timeout hit
            return true;
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
    gbm_bo* bo = nullptr;

    unique_fd fd;
    uint64_t modifier = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;

    uint32_t fourcc = 0;
    uint64_t plane_size = 0;

    gbmBuffer() {
        if (bo)
            gbm_bo_destroy(bo);
    }
};

class VkCtx;
class ImGuiCtx;
class X11Session;

struct vk_image_res_t {
    VkImage         image               = VK_NULL_HANDLE;
    VkDeviceMemory  mem                 = VK_NULL_HANDLE;
    VkImageView     view                = VK_NULL_HANDLE;
    VkImageLayout   layout              = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct dmabuf_t {
    vk_image_res_t  image_res{};
    gbmBuffer       gbm{};
};

struct opauqe_t {
    vk_image_res_t  image_res{};
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

struct slot_t {
    dmabuf_t dmabuf{};
    opauqe_t opaque{};
    source_t source{};
    sync_t  sync{};
};

struct clientRes {
    VkDevice device;

    std::vector<slot_t> buffer;
    VkCommandPool   cmd_pool            = VK_NULL_HANDLE;
    std::mutex      m;
    std::mutex      table_m;
    uint32_t        server_render_minor = 0;

    std::string     client_id;
    std::shared_ptr<hudTable> table     = nullptr;
    uint32_t        w                   = 0;
    uint32_t        h                   = 0;

    bool            send_dmabuf         = false;
    bool            reinit_dmabuf       = false;
    bool            initialized         = false;

    float           fps_limit           = 0;
    Resolution      resolution          {};

    std::shared_ptr<VkCtx> vk;
    std::shared_ptr<X11Session> x11;

    clientRes() {
        table = std::make_shared<hudTable>();
    }

    void reinit();
    ~clientRes();
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

__attribute__((unused))
void destroy_client_res(clientRes* r);

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
