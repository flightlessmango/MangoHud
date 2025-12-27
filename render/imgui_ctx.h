#pragma once
#include <vulkan/vulkan.h>
#include "vulkan_ctx.h"
#include <deque>
#include <mutex>

#include "font/font.h"
#include "colors.h"

class ImGuiCtx {
    public:
        VulkanContext& vk;
        std::vector<uint64_t> drmFormatModifiers;
        ImGuiCtx(VulkanContext& vk);
        bool init(VkDescriptorPool pool, VkFormat colorFmt);
        void add_to_queue(frame& frame_);
        void drain_queue();
        ImVec2 Text(ImVec4 col, const char* buffer);
        void render(frame& frame_);

        void teardown();
        ~ImGuiCtx() {
            teardown();
        }

    private:
        VkDescriptorPool desc_pool;
        VkCommandBuffer cmd;
        VkCommandPool cmd_pool;
        VkFence fence;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        std::deque<frame> frame_queue;
        std::mutex queue_m;
        ColorCache colors;
        std::unique_ptr<Font> fonts;

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
        void right_aligned(ImVec4& col, const char *fmt, ...);
        void RenderOutlinedText(ImVec4 textColor, const char* text);
};
