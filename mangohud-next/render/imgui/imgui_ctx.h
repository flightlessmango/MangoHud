#pragma once
#include <vulkan/vulkan.h>
#include <deque>
#include <mutex>

#include "imgui.h"
#include "../vulkan_ctx.h"
#include "font/font.h"
#include "../colors.h"
#include "implot.h"

#include "../shared.h"

class VkCtx;
class ImGuiVK;
class ImGuiEGL;

struct HudBox {
    ImVec2 pos = {};
    ImVec2 size = {};
};

struct HudLayout {
    int cols = 0;
    std::vector<float> max_cell_w;
    std::vector<float> max_value_w;
    std::vector<float> max_unit_w;
    std::vector<HudBox> col_boxes;
    std::vector<HudBox> row_boxes;
    ImVec2 content_size = {};

};

class ImGuiCtx {
public:
    std::shared_ptr<ImGuiEGL> egl;
    VkSemaphore sema = VK_NULL_HANDLE;
    inline static std::mutex m;

    ImGuiCtx();
    bool init();
    bool draw(clientRes* r, slot_t* buf, Backend backend);

    void init_vk(std::shared_ptr<VkCtx> vk_);
    void init_egl();

    void teardown();
    ~ImGuiCtx() {
        teardown();
    }

private:
    std::shared_ptr<ImGuiVK> vk;
    ImVec2 Text(ImVec4 col, const char* buffer);
    inline static float ralign_width = 0.0f;
    int buffer_size = 0;

    inline static ColorCache colors;

    void record_cmd(slot_t& buf, uint32_t w, uint32_t h);
    static uint32_t calculate_width(const HudLayout& L, const HudWindow& window);
    static uint32_t calculate_height(const HudLayout& L, const HudWindow& window);
    static void right_aligned(const ImVec4& col, float off_x, const char *fmt, ...);
    static void RenderOutlinedText(ImVec4 textColor, const char* text);
    static void draw_value_with_unit(int col_index, const TextCell& tc, const ImVec4& unit_col, const HudLayout& L, Font* fonts, const hudTable& table, float row_h, float cell_w = 0.0f, bool numeric_align = false);
    static void draw_graph_plot(const TextCell& tc, float width);
    static void draw_graph_header(const TextCell& tc, Font* fonts, const hudTable& table, const HudLayout& L);
    static void draw_progress_bar(const ProgressCell& pc, Font* fonts, const hudTable& table, float width, float height);
    static void draw_table(hudTable& table, Font* fonts, const HudLayout& layout, bool first_col_numeric = false);
    static void begin_window(const HudWindow& window, ImVec2 size, const char* name);
    static void end_window();
    static HudLayout build_layout(hudTable* table, Font* fonts);
};
