#pragma once
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <cstdint>
#include <memory>
#include <deque>
#include <mutex>
#include "shared.h"

class ImGuiCtx;

class VkCtx {
public:
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
    VkFormat fmt = VK_FORMAT_B8G8R8A8_UNORM;
    std::mutex m;
    PFN_vkImportSemaphoreFdKHR pfn_vkImportSemaphoreFdKHR = nullptr;

    explicit VkCtx(int64_t renderer = -1);
    int64_t renderer = -1;

    bool submit(std::vector<BufferSet>& buffers, uint32_t w, uint32_t h, Resolution& size,
                int idx, bool use_opaque, std::shared_ptr<HudConfig> hud, std::mutex& hud_m);
    bool init_client(std::vector<BufferSet>& buffers, VkCommandPool& cmd_pool,
                     uint32_t w, uint32_t h, size_t buffer_size = 0, bool use_opaque = false);
    void init_imgui();
    void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    bool create_sync(BufferSet* buffer);
    bool create_cmd(VkCommandPool& cmd_pool, sync_t* s);
    // int get_semaphore_fd(VkSemaphore sema);
    int get_fence_fd(VkFence fence);

    ~VkCtx();
private:
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    PFN_vkGetMemoryFdPropertiesKHR pfn_vkGetMemoryFdPropertiesKHR = nullptr;
    PFN_vkGetSemaphoreFdKHR pfn_vkGetSemaphoreFdKHR = nullptr;
    PFN_vkGetFenceFdKHR pfn_vkGetFenceFdKHR = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT pfn_vkSetDebugUtilsObjectNameEXT = nullptr;
    std::once_flag imgui_once;
    std::shared_ptr<ImGuiCtx> imgui;

    unique_fd phys_fd_;

    void init(bool enableValidation);
    int phys_fd();
    uint32_t compatible_bits_for_dmabuf_import(VkImage image, int import_fd);
    bool create_src(uint32_t w, uint32_t h, source_t* source);
    bool create_image(VkImageDrmFormatModifierExplicitCreateInfoEXT* drm, uint32_t w, uint32_t h, VkImage& image,
                      VkImageUsageFlags usage, VkImageTiling tiling, VkExternalMemoryHandleTypeFlags handle);
    bool create_dmabuf(uint32_t w, uint32_t h, dmabuf_t* buf);
    bool create_opaque(uint32_t w, uint32_t h, opauqe_t* opaque);
    bool allocate_memory(VkImage image, VkDeviceMemory& memory,
                            VkDeviceSize* allocSize, bool external, bool import_dmabuf,
                            VkExternalMemoryHandleTypeFlags handleType, int fd);
    bool create_view(VkImage image, VkDeviceMemory memory, VkImageView& view, VkFormat fmt);
    int export_opaquefd(VkDeviceMemory mem);
    uint32_t find_mem_type(uint32_t bits, VkMemoryPropertyFlags required);
    void copy_to_dst(VkImage dst, VkImageLayout& curLayout, VkImageLayout finalLayout,
                     uint32_t w, uint32_t h, BufferSet& buf);
    vkb::PhysicalDevice pick_device(vkb::Instance instance);

    void SetName(VkDevice device, VkObjectType type, uint64_t handle, const char* fmt, ...) {
        if (handle == 0)
            return;

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

        pfn_vkSetDebugUtilsObjectNameEXT(device, &info);
    }
};
