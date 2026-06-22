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

struct HudLayout {
    int cols = 0;
    std::vector<float> max_value_w;
    std::vector<float> max_unit_w;
    std::vector<float> col_content_w;

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
    inline static float unit_gap = -1.5f;
    inline static float outline_padding_x = 1.5f;
    int buffer_size = 0;

    inline static ColorCache colors;

    void record_cmd(slot_t& buf, uint32_t w, uint32_t h);
    static uint32_t calculate_width(const HudLayout& L);
    static void right_aligned(const ImVec4& col, float off_x, const char *fmt, ...);
    static void RenderOutlinedText(ImVec4 textColor, const char* text);
    static void draw_value_with_unit(int col_index, const TextCell& tc, const ImVec4& unit_col, const HudLayout& L, Font* fonts, const hudTable& table, float row_h);
    static void draw_graph_plot(const TextCell& tc);
    static void draw_graph_header(const TextCell& tc, Font* fonts, const hudTable& table);
    static HudLayout build_layout(hudTable* table, Font* fonts);
};
