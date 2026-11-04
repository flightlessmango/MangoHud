#pragma once
#include "vkroots.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <shared_mutex>
#include "vulkan/vk_enum_string_helper.h"
#include "../ipc/ipc_client.h"
#include "../render/colors.h"
#include "../render/shared.h"

struct swapchain_data;
struct dmabuf_ext;
struct overlay_frame {
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore done = VK_NULL_HANDLE;
    std::shared_ptr<dmabuf_ext> hud;
    int pending_slot = -1;
    uint64_t import_generation = 0;
};
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
    VkSemaphore semaphore = VK_NULL_HANDLE;
    std::shared_ptr<overlay_resources> ovl_res;
    bool inited = false;
    bool valid = false;

    struct use {
        overlay_frame* frame;
        VkQueue queue;
        bool writing;
    };
    std::mutex uses_m;
    std::vector<use> uses;

    VkResult wait_for_use(VkQueue queue, bool updating);
    void release_use(const overlay_frame* frame);

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
    std::shared_mutex m;
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

        reset_images();
    }
};

struct overlay_hud_frame {
    std::shared_ptr<dmabuf_ext> buffer;
    int slot = -1;
    bool refresh_cache = false;
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
    std::vector<std::shared_ptr<dmabuf_ext>> dmabufs;

    OverlayVK(Layer* layer_) : layer(layer_) {}

    bool draw(const swapchain_data* sc, overlay_frame& frame,
              uint32_t img_idx, VkQueue queue, VkPresentInfoKHR pi);

    ~OverlayVK() {
        for (auto& b : dmabufs)
            b.reset();
    }

private:
    std::shared_mutex m;
    std::mutex selection_m;
    overlay_hud_frame prepare_hud_frame(overlay_frame& frame);
    VkFormat fmt = VK_FORMAT_B8G8R8A8_UNORM;
    int last_slot = -1;
    std::deque<int> frame_queue;
    bool inited = false;
    uint64_t import_generation = 0;

    bool init_dmabufs(const swapchain_data* sc, Fdinfo& fdinfo);
    VkResult import_dmabuf(const swapchain_data* sc, dmabuf_ext* buf, unique_fd& import_fd, Fdinfo& fdinfo);
    VkResult copy_dmabuf_to_cache(const swapchain_data* sc, overlay_frame& frame,
                                  const overlay_hud_frame& hud, VkQueue queue, int img_idx, VkPresentInfoKHR pi);
    bool cache_descriptor_set(const swapchain_data* sc, std::shared_ptr<dmabuf_ext>& buf);

    void update_hud_cache(const swapchain_data* sc, VkCommandBuffer cmd, const dmabuf_ext* buf);
    void draw_hud_cache(const swapchain_data* sc, VkCommandBuffer cmd, int img_idx, const dmabuf_ext* buf);

    uint32_t find_mem_type_import(const swapchain_data* sc, const VkImage image, int fd);
    uint32_t find_mem_type_image(const swapchain_data* sc, uint32_t type_bits, VkMemoryPropertyFlags want);
    void transition_image(const swapchain_data* sc, VkCommandBuffer cmd, VkImage image,
                          VkImageLayout old_layout,VkImageLayout new_layout);
    void wait_on_semaphores(const vkroots::VkDeviceDispatch* d);
    void cache_to_transfer_dst(const swapchain_data* sc, VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout);
    void cache_to_shader_read(const swapchain_data* sc, VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout);
};
