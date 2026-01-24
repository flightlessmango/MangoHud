#pragma once
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <memory>
#include <deque>
#include "imgui_ctx.h"
#include "shared.h"

class VkCtx {
public:
    std::unique_ptr<ImGuiCtx> imgui;

    VkCtx(int64_t renderMinor) : renderMinor(renderMinor) {
        SPDLOG_DEBUG("VkCtx init with renderMinor: {}", renderMinor);
        init(true);
        imgui = std::make_unique<ImGuiCtx>(device, physicalDevice, instance,
                                          graphicsQueue, graphicsQueueFamilyIndex, fmt);
    };

    void queue_frame(clientRes& r);
    std::deque<clientRes*> drain_queue();
    void submit(clientRes& r);
    clientRes& init_client(clientRes& r);

    ~VkCtx();
private:
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
    VkFormat fmt = VK_FORMAT_B8G8R8A8_SRGB;

    VkFence fence;
    VkSemaphore in_flight_release_sem = VK_NULL_HANDLE;

    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    PFN_vkGetMemoryFdPropertiesKHR pfn_vkGetMemoryFdPropertiesKHR = nullptr;
    PFN_vkGetSemaphoreFdKHR pfn_vkGetSemaphoreFdKHR = nullptr;
    int phys_fd_ = -1;
    int64_t renderMinor;
    std::mutex m;
    std::deque<clientRes*> frame_queue;
    bool use_dmabuf = true;

    void init(bool enableValidation);
    int phys_fd();
    uint32_t compatible_bits_for_dmabuf_import(VkImage image, int import_fd);
    bool create_gbm_buffer(clientRes& r);
    bool create_src(clientRes& r);
    bool create_image(VkImageDrmFormatModifierExplicitCreateInfoEXT* drm, clientRes& r, VkImage& image,
                      VkImageUsageFlags usage, VkImageTiling tiling, VkExternalMemoryHandleTypeFlags handle);
    bool create_dmabuf(clientRes& r);
    bool create_opaque(clientRes& r);
    bool allocate_memory(VkImage image, VkDeviceMemory& memory, clientRes& r,
                            VkDeviceSize* allocSize, bool external, bool import_dmabuf,
                            VkExternalMemoryHandleTypeFlags handleType);
    bool create_view(VkImage image, VkDeviceMemory memory, VkImageView& view, VkFormat fmt);
    int export_opaquefd(VkDeviceMemory mem);
    uint32_t find_mem_type(uint32_t bits, VkMemoryPropertyFlags required);
    void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copy_to_dst(VkImage dst, VkImageLayout& curLayout, VkImageLayout finalLayout, clientRes& r);
    vkb::PhysicalDevice pick_device(vkb::Instance instance);
};
