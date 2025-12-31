#pragma once
#include <vulkan/vulkan.h>
#include "vulkan_ctx.h"
#include <deque>
#include <mutex>

#include "font/font.h"
#include "colors.h"
struct ClientUiState {
    ImVec2 window_size {500, 500};
};

class ImGuiCtx {
    public:
        VulkanContext& vk;
        std::vector<uint64_t> drmFormatModifiers;
        ImGuiCtx(VulkanContext& vk);
        bool init(VkDescriptorPool pool, VkFormat colorFmt);
        void add_to_queue(frame frame_);
        std::deque<frame> drain_queue();
        ImVec2 Text(ImVec4 col, const char* buffer);
        void render(frame& frame_);
        std::unordered_map<std::string, ClientUiState> client_states;

        int take_fence_for_client(const std::string& id) {
            std::lock_guard<std::mutex> lock(fences_mtx);
            auto it = client_fences.find(id);
            if (it == client_fences.end())
                return -1;

            int fd = it->second;
            client_fences.erase(it);
            return fd;
        }

        void teardown();
        ~ImGuiCtx() {
            teardown();
        }

        private:
        VkDescriptorPool desc_pool;
        VkCommandBuffer cmd;
        VkCommandPool cmd_pool;
        VkFence fence;
        VkSemaphore in_flight_acquire_sem = VK_NULL_HANDLE;
        VkSemaphore in_flight_release_sem = VK_NULL_HANDLE;

        PFN_vkImportSemaphoreFdKHR vkImportSemaphoreFdKHR = nullptr;
        PFN_vkGetSemaphoreFdKHR    vkGetSemaphoreFdKHR = nullptr;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        std::deque<frame> frame_queue;
        std::mutex queue_m;
        ColorCache colors;
        std::unique_ptr<Font> fonts;
        std::mutex mtx;
        std::mutex fences_mtx;
        std::unordered_map<std::string, int> client_fences;

        VkDescriptorPool create_desc_pool(VkDevice dev);

        void create_cmd(VkDevice dev, uint32_t qfam);
        void submit_draw(frame& frame_);
        static void transition_image(VkCommandBuffer cmd,
                                    VkImage image,
                                    VkImageLayout oldLayout,
                                    VkImageLayout newLayout);
        uint32_t find_mem_type(uint32_t typeBits, VkMemoryPropertyFlags desired);
        float calculate_width(frame& frame_);
        float calculate_height(frame& frame_);
        void right_aligned(const ImVec4& col, float off_x, const char *fmt, ...);
        void RenderOutlinedText(ImVec4 textColor, const char* text);
        VkSemaphore make_syncfd_semaphore();
};
