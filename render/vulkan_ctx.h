#pragma once
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <memory>
#include <deque>
#include "imgui_ctx.h"
#include "shared.h"
#include "../ipc/client.h"

class VkCtx {
public:
    std::shared_ptr<ImGuiCtx> imgui;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
    VkFormat fmt = VK_FORMAT_B8G8R8A8_UNORM;
    std::mutex m;
    PFN_vkImportSemaphoreFdKHR pfn_vkImportSemaphoreFdKHR = nullptr;

    VkCtx() {
        init(true);
        imgui = std::make_shared<ImGuiCtx>(this, 4);
    };

    bool submit(std::shared_ptr<clientRes>& r, int idx);
    void init_client(clientRes* r, size_t buffer_size = 0);
    void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void create_sync(slot_t* s);
    void create_cmd(clientRes* r, sync_t* s);
    void wait_on_semaphores(std::shared_ptr<Client> client);
    void sync_wait(std::shared_ptr<Client> client);
    // int get_semaphore_fd(VkSemaphore sema);
    int get_fence_fd(VkFence fence);

    ~VkCtx();
private:
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    PFN_vkGetMemoryFdPropertiesKHR pfn_vkGetMemoryFdPropertiesKHR = nullptr;
    PFN_vkGetSemaphoreFdKHR pfn_vkGetSemaphoreFdKHR = nullptr;
    PFN_vkGetFenceFdKHR pfn_vkGetFenceFdKHR = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT pfn_vkSetDebugUtilsObjectNameEXT = nullptr;

    unique_fd phys_fd_;
    gbm_device* gbm_dev = nullptr;
    bool use_dmabuf = true;

    void init(bool enableValidation);
    int phys_fd();
    uint32_t compatible_bits_for_dmabuf_import(VkImage image, int import_fd);
    bool create_gbm_buffer(clientRes* r, dmabuf_t* buf);
    bool create_src(clientRes* r, source_t* source);
    bool create_image(VkImageDrmFormatModifierExplicitCreateInfoEXT* drm, clientRes* r, VkImage& image,
                      VkImageUsageFlags usage, VkImageTiling tiling, VkExternalMemoryHandleTypeFlags handle);
    bool create_dmabuf(clientRes*r, dmabuf_t* buf);
    bool create_opaque(clientRes* r, opauqe_t* opaque);
    bool allocate_memory(VkImage image, VkDeviceMemory& memory, clientRes* r,
                            VkDeviceSize* allocSize, bool external, bool import_dmabuf,
                            VkExternalMemoryHandleTypeFlags handleType, int fd);
    bool create_view(VkImage image, VkDeviceMemory memory, VkImageView& view, VkFormat fmt);
    int export_opaquefd(VkDeviceMemory mem);
    uint32_t find_mem_type(uint32_t bits, VkMemoryPropertyFlags required);
    void copy_to_dst(VkImage dst, VkImageLayout& curLayout, VkImageLayout finalLayout, clientRes* r, slot_t& buf);
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
