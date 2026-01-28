#pragma once
#include "vkroots.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include "../ipc/ipc_client.h"
#include "../render/colors.h"
#include "../render/shared.h"

struct dmabuf {
    bool valid = false;
    // identity for re-import decisions
    int fd = -1;
    uint32_t width = 0, height = 0;
    uint32_t fourcc = 0;
    uint64_t modifier = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;
    uint64_t plane_size = 0;

    const vkroots::VkDeviceDispatch* d = nullptr;

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage cache_image = VK_NULL_HANDLE;
    VkDeviceMemory cache_mem = VK_NULL_HANDLE;
    VkImageView cache_view = VK_NULL_HANDLE;
    VkFormat cache_format = VK_FORMAT_UNDEFINED;
    VkImageLayout cache_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    bool layout_ready = false;

    ~dmabuf() {
        if (!d)
            return;

        d->DeviceWaitIdle(d->Device);

        if (view) {
            d->DestroyImageView(d->Device, view, nullptr);
            view = VK_NULL_HANDLE;
        }

        if (image) {
            d->DestroyImage(d->Device, image, nullptr);
            image = VK_NULL_HANDLE;
        }

        if (mem) {
            d->FreeMemory(d->Device, mem, nullptr);
            mem = VK_NULL_HANDLE;
        }

        if (cache_view) {
            d->DestroyImageView(d->Device, cache_view, nullptr);
            cache_view = VK_NULL_HANDLE;
        }

        if (cache_image) {
            d->DestroyImage(d->Device, cache_image, nullptr);
            cache_image = VK_NULL_HANDLE;
        }

        if (cache_mem) {
            d->FreeMemory(d->Device, cache_mem, nullptr);
            cache_mem = VK_NULL_HANDLE;
        }
    }
};

struct overlay_pipeline {
    const vkroots::VkDeviceDispatch* d = nullptr;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkDescriptorPool dp = VK_NULL_HANDLE;
    VkDescriptorSet ds = VK_NULL_HANDLE;

    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule fs = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;

    ~overlay_pipeline() {
        if (!d) return;

        if (dp) {
            d->DestroyDescriptorPool(d->Device, dp, nullptr);
            dp = VK_NULL_HANDLE;
            ds = VK_NULL_HANDLE;
        }

        if (pipe) {
            d->DestroyPipeline(d->Device, pipe, nullptr);
            pipe = VK_NULL_HANDLE;
        }

        if (pl) {
            d->DestroyPipelineLayout(d->Device, pl, nullptr);
            pl = VK_NULL_HANDLE;
        }

        if (dsl) {
            d->DestroyDescriptorSetLayout(d->Device, dsl, nullptr);
            dsl = VK_NULL_HANDLE;
        }

        if (sampler) {
            d->DestroySampler(d->Device, sampler, nullptr);
            sampler = VK_NULL_HANDLE;
        }

        if (vs) {
            d->DestroyShaderModule(d->Device, vs, nullptr);
            vs = VK_NULL_HANDLE;
        }

        if (fs) {
            d->DestroyShaderModule(d->Device, fs, nullptr);
            fs = VK_NULL_HANDLE;
        }

        d = nullptr;
    }
};

struct swapchain_data {
    const vkroots::VkDeviceDispatch* d = nullptr;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkColorSpaceKHR colorspace;

    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> fb;

    VkRenderPass rp = VK_NULL_HANDLE;

    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmd;
    std::vector<VkFence> cmd_fences;

    std::vector<VkSemaphore> overlay_done;
    std::unique_ptr<overlay_pipeline> ovl;

    swapchain_data() {
        ovl = std::make_unique<overlay_pipeline>();
    }

    ~swapchain_data() {
        if (!d)
            return;

        d->DeviceWaitIdle(d->Device);

        for (auto& sema : overlay_done) {
            d->DestroySemaphore(d->Device, sema, nullptr);
            sema = VK_NULL_HANDLE;
        }

        for (auto& framebuf : fb)
            if (framebuf)
                d->DestroyFramebuffer(d->Device, framebuf, nullptr);

        for (auto& view : views)
            if (view)
                d->DestroyImageView(d->Device, view, nullptr);

        if (rp)
            d->DestroyRenderPass(d->Device, rp, nullptr);

        if (!cmd_fences.empty())
            for (auto& fence : cmd_fences)
                d->DestroyFence(d->Device, fence, nullptr);

        if (cmd_pool && !cmd.empty()) {
            d->FreeCommandBuffers(d->Device, cmd_pool,
                                         (uint32_t)cmd.size(), cmd.data());
        }

        if (cmd_pool)
            d->DestroyCommandPool(d->Device, cmd_pool, nullptr);
    }
};

struct OverlayPushConsts {
    float dstExtent[2];
    float srcExtent[2];
    float offsetPx[2];
    uint32_t transfer_function;
};

static const uint32_t overlay_vert_spv[] = {
    #include "overlay.vert.spv.h"
};
static const uint32_t overlay_frag_spv[] = {
    #include "overlay.frag.spv.h"
};

class OverlayVK {
public:
    PFN_vkSetDeviceLoaderData loader_data = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT g_vkSetDebugUtilsObjectNameEXT = nullptr;
    IPCClient* ipc;
    std::mutex m;
    std::mutex swapchain_mtx;
    std::unordered_map<VkSwapchainKHR, std::shared_ptr<swapchain_data>> swapchains;

    OverlayVK(PFN_vkSetDeviceLoaderData loader_data_, IPCClient* ipc_) : loader_data(loader_data_), ipc(ipc_) {}

    bool draw(const vkroots::VkDeviceDispatch* d, VkSwapchainKHR swapchain,
            uint32_t family, uint32_t image_count, uint32_t img_idx,
            VkPresentInfoKHR* pPresentInfo, VkQueue queue);

    void SetName(VkDevice device, VkObjectType type, uint64_t handle, const char* fmt, ...) {
        if (!g_vkSetDebugUtilsObjectNameEXT || handle == 0) {
            return;
        }

        char name[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(name, sizeof(name), fmt, args);
        va_end(args);

        VkDebugUtilsObjectNameInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = type;
        info.objectHandle = handle;
        info.pObjectName = name;

        g_vkSetDebugUtilsObjectNameEXT(device, &info);
    }

    ~OverlayVK() {
        for (auto [swap, swap_data] : swapchains)
            swap_data.reset();

        ext.reset();
        if (release_sema) {
            dispatch->DestroySemaphore(dispatch->Device, release_sema, nullptr);
            release_sema = VK_NULL_HANDLE;
        }

        if (acquire_sema) {
            dispatch->DestroySemaphore(dispatch->Device, acquire_sema, nullptr);
            acquire_sema = VK_NULL_HANDLE;
        }

        if (release_fence) {
            dispatch->DestroyFence(dispatch->Device, release_fence, nullptr);
            release_fence = VK_NULL_HANDLE;
        }
    }

private:
    int acquire_fd = -1;
    std::unique_ptr<dmabuf> ext;
    const vkroots::VkDeviceDispatch* dispatch;
    VkSemaphore release_sema = VK_NULL_HANDLE;
    VkSemaphore acquire_sema = VK_NULL_HANDLE;
    VkFence release_fence = VK_NULL_HANDLE;

    VkResult pipeline(const vkroots::VkDeviceDispatch* d, swapchain_data* sc);
    VkResult cmd_resources(const vkroots::VkDeviceDispatch* d,
                        swapchain_data* sc, uint32_t family, uint32_t image_count);
    VkResult sample_dmabuf(const vkroots::VkDeviceDispatch* d, swapchain_data* sc,
                        uint32_t imageIndex, uint32_t w, uint32_t h, bool refresh_cache,
                        float offset_x_px = 0.0f, float offset_y_px = 0.0f);
    VkResult semaphores(const vkroots::VkDeviceDispatch* d, swapchain_data* sc,
                        uint32_t img_idx, VkPresentInfoKHR* pPresentInfo,
                        VkQueue queue, bool frame_ready);
    VkResult import_dmabuf(const vkroots::VkDeviceDispatch* d,
                           const VkAllocationCallbacks* alloc = nullptr);

    VkShaderModule make_shader(const vkroots::VkDeviceDispatch* d,VkDevice dev,
                                        const uint32_t* code, size_t codeSizeBytes);
    std::shared_ptr<swapchain_data> get_swapchain_data(VkSwapchainKHR swap);
    uint32_t find_mem_type(const vkroots::VkDeviceDispatch* d, const VkImage image);
};

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
