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

struct swapchain_data;
struct overlay_resources;
struct cached_image {
    std::shared_ptr<const vkroots::VkDeviceDispatch> d;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkDescriptorSet ds = VK_NULL_HANDLE;
    VkDescriptorPool dp = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    std::shared_ptr<overlay_resources> ovl_res;
    bool inited = false;
    bool valid = false;

    cached_image(std::shared_ptr<const vkroots::VkDeviceDispatch> d_) : d(d_) {
        VkExportSemaphoreCreateInfo exportInfo{
            .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
        };

        VkSemaphoreCreateInfo sci{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &exportInfo,
            .flags = 0,
        };

        VkResult r = vkCreateSemaphore(d->Device, &sci, nullptr, &semaphore);
        if (r != VK_SUCCESS)
            SPDLOG_ERROR("vkCreateSemaphore {}", string_VkResult(r));
    }
    ~cached_image();
};
class Layer;
struct dmabuf_ext {
    bool valid = false;
    uint32_t width = 0, height = 0;
    uint32_t fourcc = 0;
    uint64_t modifier = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;
    uint64_t plane_size = 0;

    std::shared_ptr<const vkroots::VkDeviceDispatch> d;

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

    std::shared_ptr<cached_image> cached;

    bool needs_import = true;
    bool layout_ready = false;

    dmabuf_ext(std::shared_ptr<const vkroots::VkDeviceDispatch> d_) :
               d(d_), cached(std::make_shared<cached_image>(d)) {}

    void reset_images() {
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

        cached = std::make_shared<cached_image>(d);
    }

    ~dmabuf_ext() {
        if (!d)
            return;

        d->DeviceWaitIdle(d->Device);

        reset_images();
    }
};

struct OverlayPushConsts {
    float dstExtent[2];
    float srcExtent[2];
    float offsetPx[2];
    uint32_t transfer_function;
};

class OverlayVK {
public:
    Layer* layer;
    std::mutex m;
    std::vector<std::shared_ptr<dmabuf_ext>> dmabufs;

    OverlayVK(Layer* layer_) : layer(layer_) {}

    bool draw(VkSwapchainKHR swapchain, uint32_t img_idx, VkQueue queue);
    std::vector<int> init_dmabufs(Fdinfo& fdinfo);

    ~OverlayVK() {
        for (auto& b : dmabufs)
            b.reset();
    }

private:
    VkFormat fmt = VK_FORMAT_B8G8R8A8_SRGB;
    int current_slot = -1;
    int last_slot = -1;
    std::deque<int> frame_queue;
    bool inited = false;
    std::shared_ptr<swapchain_data> sc;
    VkQueue queue;

    VkResult import_dmabuf(dmabuf_ext* buf, unique_fd& import_fd, Fdinfo& fdinfo);
    VkResult copy_dmabuf_to_cache(VkQueue queue, int img_idx);
    void cache_descriptor_set(std::shared_ptr<dmabuf_ext>& buf);

    uint32_t find_mem_type(const VkImage image, int fd);
    void transition_image(VkCommandBuffer cmd, VkImage image,
                          VkImageLayout old_layout,VkImageLayout new_layout);
    void wait_on_semaphores(const vkroots::VkDeviceDispatch* d);
    void cache_to_transfer_dst(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout);
    void cache_to_shader_read(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout);
};
