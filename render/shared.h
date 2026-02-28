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
    int64_t renderMinor;

    gbmBuffer() {
        if (bo)
            gbm_bo_destroy(bo);
    }
};

class VkCtx;
class ImGuiCtx;

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
    VkSemaphore             semaphore           = VK_NULL_HANDLE;
    unique_fd               semaphore_fd;
    VkSemaphore             consumer_semaphore  = VK_NULL_HANDLE;
    unique_fd               consumer_semaphore_fd;
    uint64_t                semaphore_last = 1;
    uint64_t                consumer_last = 0;
    VkCommandBuffer         cmd                 = VK_NULL_HANDLE;
    VkFence                 fence               = VK_NULL_HANDLE;
    bool                    inited              = false;
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
    std::vector<VkFence> in_flight;
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

    std::shared_ptr<VkCtx> vk;
    std::shared_ptr<ImGuiCtx> imgui;

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

static inline const char* string_VkResult(VkResult input_value) {
    switch (input_value) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN:
            return "VK_ERROR_UNKNOWN";
        case VK_ERROR_VALIDATION_FAILED:
            return "VK_ERROR_VALIDATION_FAILED";
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:
            return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
            return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        case VK_ERROR_FRAGMENTATION:
            return "VK_ERROR_FRAGMENTATION";
        case VK_PIPELINE_COMPILE_REQUIRED:
            return "VK_PIPELINE_COMPILE_REQUIRED";
        case VK_ERROR_NOT_PERMITTED:
            return "VK_ERROR_NOT_PERMITTED";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
            return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_INVALID_SHADER_NV:
            return "VK_ERROR_INVALID_SHADER_NV";
        case VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR:
            return "VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR:
            return "VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR:
            return "VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR:
            return "VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR:
            return "VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR:
            return "VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR";
        case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
            return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
        case VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT:
            return "VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT";
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
            return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
        case VK_THREAD_IDLE_KHR:
            return "VK_THREAD_IDLE_KHR";
        case VK_THREAD_DONE_KHR:
            return "VK_THREAD_DONE_KHR";
        case VK_OPERATION_DEFERRED_KHR:
            return "VK_OPERATION_DEFERRED_KHR";
        case VK_OPERATION_NOT_DEFERRED_KHR:
            return "VK_OPERATION_NOT_DEFERRED_KHR";
        case VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR:
            return "VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR";
        case VK_ERROR_COMPRESSION_EXHAUSTED_EXT:
            return "VK_ERROR_COMPRESSION_EXHAUSTED_EXT";
        case VK_INCOMPATIBLE_SHADER_BINARY_EXT:
            return "VK_INCOMPATIBLE_SHADER_BINARY_EXT";
        case VK_PIPELINE_BINARY_MISSING_KHR:
            return "VK_PIPELINE_BINARY_MISSING_KHR";
        case VK_ERROR_NOT_ENOUGH_SPACE_KHR:
            return "VK_ERROR_NOT_ENOUGH_SPACE_KHR";
        default:
            return "Unhandled VkResult";
    }
}

