#pragma once
#include <vulkan/vulkan.h>
#include <deque>
#include <mutex>

#include "imgui.h"
#include "vulkan_ctx.h"
#include "font/font.h"
#include "colors.h"
#include "implot.h"
#include "backends/imgui_impl_vulkan.h"
#include "shared.h"

struct HudLayout {
    float value_field_w = 0.0f;
    int cols = 0;
    std::vector<float> max_unit_w;
    std::vector<float> col_content_w;

};

class ImGuiCtx {
public:
    VkCommandBuffer cmd;

    ImGuiCtx(VkDevice device_, VkPhysicalDevice phys_, VkInstance instance,
                VkQueue queue_, uint32_t queue_idx_, VkFormat fmt_);
    bool init();
    void draw(std::shared_ptr<clientRes>& r);

    void teardown();
    ~ImGuiCtx() {
        teardown();
    }

private:
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkInstance instance;
    VkQueue graphicsQueue;
    uint32_t graphicsQueueIdx;
    VkDescriptorPool desc_pool;
    VkCommandPool cmd_pool;
    VkFormat fmt;

    ImGuiContext* imgui = nullptr;
    ImPlotContext* implot = nullptr;
    ImVec2 Text(ImVec4 col, const char* buffer);
    float ralign_width = 0;
    float unit_gap = 1.0f;
    float outline_padding_x = 1.5f;

    ColorCache colors;
    std::unique_ptr<Font> fonts;
    std::mutex mtx;

    VkDescriptorPool create_desc_pool();
    void create_cmd();
    void transition_image(VkCommandBuffer cmd,
                                VkImage image,
                                VkImageLayout oldLayout,
                                VkImageLayout newLayout);
    uint32_t calculate_width(const HudLayout& L);
    void right_aligned(const ImVec4& col, float off_x, const char *fmt, ...);
    void RenderOutlinedText(ImVec4 textColor, const char* text);
    void draw_value_with_unit(int col_index, const TextCell& tc, const ImVec4& unit_col, const HudLayout& L);
    void draw_graph_plot(const TextCell& tc);
    void draw_graph_header(const TextCell& tc);
    HudLayout build_layout(std::shared_ptr<hudTable>& table);
};
