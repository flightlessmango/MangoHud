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
    std::shared_ptr<Font> fonts;
    VkCtx* vk;
    VkSemaphore sema = VK_NULL_HANDLE;

    ImGuiCtx(VkCtx* vk_, int buffer_size_);
    bool init();
    bool draw(std::shared_ptr<clientRes>& r, slot_t& buf);

    void teardown();
    ~ImGuiCtx() {
        teardown();
    }

private:
    VkDescriptorPool desc_pool;
    ImGuiContext* imgui;
    ImPlotContext* implot;
    ImVec2 Text(ImVec4 col, const char* buffer);
    float ralign_width = 0;
    float unit_gap = 1.0f;
    float outline_padding_x = 1.5f;
    int buffer_size = 0;

    ColorCache colors;
    std::mutex m;

    VkDescriptorPool create_desc_pool();
    uint32_t calculate_width(const HudLayout& L);
    void right_aligned(const ImVec4& col, float off_x, const char *fmt, ...);
    void RenderOutlinedText(ImVec4 textColor, const char* text);
    void draw_value_with_unit(int col_index, const TextCell& tc, const ImVec4& unit_col, const HudLayout& L);
    void draw_graph_plot(const TextCell& tc);
    void draw_graph_header(const TextCell& tc);
    void record_cmd(slot_t& buf, uint32_t w, uint32_t h);
    HudLayout build_layout(hudTable* table);
};
