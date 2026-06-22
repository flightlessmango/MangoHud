#include <deque>
#include <unistd.h>
#include <math.h>
#include "backends/imgui_impl_opengl3.h"
#include "imgui_ctx.h"
#include "imgui.h"
#include "font/font.h"
#include "../server/config.h"
#include "vk.h"
#include "egl.h"
std::mutex init_m;

ImGuiCtx::ImGuiCtx() {
    std::lock_guard lock(init_m);
};

void ImGuiCtx::init_vk(std::shared_ptr<VkCtx> vk_) {
    vk = std::make_shared<ImGuiVK>(std::move(vk_), this);
}

void ImGuiCtx::init_egl() {
    egl = std::make_shared<ImGuiEGL>(this);
}

void ImGuiCtx::right_aligned(const ImVec4& col, float off_x, const char* fmt, ...) {
    ImVec2 pos = ImGui::GetCursorPos();
    char buffer[32]{};

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    ImVec2 sz = ImGui::CalcTextSize(buffer);
    float pad_r = std::ceil(outline_padding_x);
    ImGui::SetCursorPosX(pos.x + off_x - (sz.x + pad_r));
    RenderOutlinedText(col, buffer);
}

uint32_t ImGuiCtx::calculate_width(const HudLayout& L) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const int cols = L.cols;

    const float pad_r = std::ceil(outline_padding_x);

    float total = 0.0f;

    for (int c = 0; c < cols; c++)
        total += L.col_content_w[c];

    if (cols > 1)
        total += style.ItemSpacing.x * (float)(cols - 1);

    total += (style.CellPadding.x * 2.0f) * (float)cols;
    total += style.WindowPadding.x * 2.0f;
    total += pad_r;

    return (uint32_t)std::ceil(total);
}

static inline double TransformForward_Custom(double v, void*) {
    if (v > 50)
        v = 49.9;

    return v;
}

static inline double TransformInverse_Custom(double v, void*) {
   return v;
}

static float text_font_size(const hudTable& table, const TextCell& tc) {
    if (tc.style.font_size > 0.0f)
        return tc.style.font_size;

    return table.font_size * tc.style.font_scale;
}

static float unit_font_size(const hudTable& table, const TextCell& tc) {
    return text_font_size(table, tc) / 2.0f;
}

static void prepare_table_fonts(const hudTable& table, Font* fonts) {
    fonts->get(table.font_size);
    fonts->get(table.font_size / 2.0f);

    for (const auto& row : table.rows) {
        for (const auto& opt : row) {
            if (!opt)
                continue;

            const auto* tc = std::get_if<TextCell>(&*opt);
            if (!tc)
                continue;

            fonts->get(text_font_size(table, *tc));
            fonts->get(unit_font_size(table, *tc));
        }
    }
}

static ImVec2 text_size(const hudTable& table, const TextCell& tc, Font* fonts) {
    ImGui::PushFont(fonts->get(text_font_size(table, tc)));
    ImVec2 size = ImGui::CalcTextSize(tc.text.c_str());
    ImGui::PopFont();
    return size;
}

static ImVec2 unit_size(const hudTable& table, const TextCell& tc, Font* fonts) {
    if (tc.unit.empty())
        return ImVec2(0.0f, 0.0f);

    const float size = tc.unit == "%" ? text_font_size(table, tc) : unit_font_size(table, tc);
    ImGui::PushFont(fonts->get(size));
    ImVec2 unit = ImGui::CalcTextSize(tc.unit.c_str());
    ImGui::PopFont();
    return unit;
}

static float cell_height(const hudTable& table, const TextCell& tc, Font* fonts) {
    const ImVec2 value_sz = text_size(table, tc, fonts);
    const ImVec2 unit_sz = unit_size(table, tc, fonts);
    return std::max(value_sz.y, unit_sz.y);
}

static float row_height(const hudTable& table, const std::vector<MaybeCell>& row, Font* fonts) {
    float height = 0.0f;

    for (const auto& opt : row) {
        if (!opt)
            continue;

        const auto* tc = std::get_if<TextCell>(&*opt);
        if (!tc || !tc->data.empty())
            continue;

        height = std::max(height, cell_height(table, *tc, fonts));
    }

    return height;
}

void ImGuiCtx::draw_value_with_unit(int col_index,
                                   const TextCell& tc,
                                   const ImVec4& unit_col,
                                   const HudLayout& L,
                                   Font* fonts,
                                   const hudTable& table,
                                   float row_h) {
    const ImVec2 base = ImGui::GetCursorPos();
    const float text_font_sz = text_font_size(table, tc);
    const ImVec2 value_sz = text_size(table, tc, fonts);
    const ImVec2 u_sz = unit_size(table, tc, fonts);
    const float text_y = base.y + row_h - value_sz.y;
    const float unit_y = base.y + row_h - u_sz.y;

    if (col_index == 0) {
        ImGui::SetCursorPos(ImVec2(base.x, text_y));
        ImGui::PushFont(fonts->get(text_font_sz));
        RenderOutlinedText(tc.vec, tc.text.c_str());
        ImGui::PopFont();
        if (!tc.unit.empty()) {
            ImGui::SetCursorPos(ImVec2(base.x + value_sz.x, unit_y));
            ImGui::SameLine(0.0f, unit_gap);

            if (tc.unit == "%") {
                ImGui::PushFont(fonts->get(text_font_sz));
                RenderOutlinedText(unit_col, tc.unit.c_str());
                ImGui::PopFont();
            } else {
                ImGui::PushFont(fonts->get(unit_font_size(table, tc)));
                RenderOutlinedText(unit_col, tc.unit.c_str());
                ImGui::PopFont();
            }
        }

        ImGui::SetCursorPos(ImVec2(base.x, base.y + row_h));
        return;
    }

    const float cell_left = base.x;
    const float cell_w = ImGui::GetContentRegionAvail().x;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float pad_r = std::ceil(outline_padding_x);
    const float right_pad = style.CellPadding.x + pad_r;

    const float unit_start_x = cell_left + (cell_w - right_pad - L.max_unit_w[col_index]);
    const float value_right_x = unit_start_x - unit_gap;
    const float value_w = L.max_value_w[col_index];
    const float value_left_x = value_right_x - value_w;

    ImGui::SetCursorPos(ImVec2(value_left_x, text_y));
    ImGui::PushFont(fonts->get(text_font_sz));
    right_aligned(tc.vec, value_w, "%s", tc.text.c_str());
    ImGui::PopFont();

    if (!tc.unit.empty()) {
        ImGui::SetCursorPos(ImVec2(unit_start_x, unit_y));
        if (tc.unit == "%") {
            ImGui::PushFont(fonts->get(text_font_sz));
            RenderOutlinedText(unit_col, tc.unit.c_str());
            ImGui::PopFont();
        } else {
            ImGui::PushFont(fonts->get(unit_font_size(table, tc)));
            RenderOutlinedText(unit_col, tc.unit.c_str());
            ImGui::PopFont();
        }
    }

    ImGui::SetCursorPos(ImVec2(base.x, base.y + row_h));
}

void ImGuiCtx::draw_graph_header(const TextCell& tc, Font* fonts, const hudTable& table) {
    ImGui::TableSetColumnIndex(0);
    ImGui::PushFont(fonts->get(unit_font_size(table, tc)));
    RenderOutlinedText(colors.get("eb5b5b"), "frametime");

    ImGui::TableSetColumnIndex(ImGui::TableGetColumnCount() - 1);
    float max = *std::max_element(tc.data.begin(), tc.data.end());
    float min = *std::min_element(tc.data.begin(), tc.data.end());
    right_aligned(colors.get("FFFFFF"), ralign_width, "min: %.1fms, max: %.1fms", min, max);
    ImGui::PopFont();
}

void ImGuiCtx::draw_graph_plot(const TextCell& tc) {
    auto& style = ImGui::GetStyle();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushID(&tc);
    float width = ImGui::GetWindowContentRegionMax().x - (style.WindowPadding.x * 2);
    if (ImGui::BeginChild("my_child_window", ImVec2(width, 50), false, ImGuiWindowFlags_NoDecoration)) {
        if (ImPlot::BeginPlot("My Plot", ImVec2(width, 50), ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs)) {
            ImPlotStyle& implot_style = ImPlot::GetStyle();
            implot_style.Colors[ImPlotCol_PlotBg]      = ImVec4(0.92f, 0.92f, 0.95f, 0.00f);
            implot_style.Colors[ImPlotCol_AxisGrid]    = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
            implot_style.Colors[ImPlotCol_AxisTick]    = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
            implot_style.Colors[ImPlotCol_FrameBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            implot_style.PlotPadding.x = 0;
            ImPlotAxisFlags ax_flags_x = ImPlotAxisFlags_NoDecorations;
            ImPlotAxisFlags ax_flags_y = ImPlotAxisFlags_NoDecorations;
            ImPlot::SetupAxes(nullptr, nullptr, ax_flags_x, ax_flags_y);
            ImPlot::SetupAxisScale(ImAxis_Y1, TransformForward_Custom, TransformInverse_Custom);
            ImPlot::SetupAxesLimits(0, 200, 0, 50, ImGuiCond_Always);
            ImPlot::SetNextLineStyle(ImVec4(0,1,0,1), 2);
            ImPlot::PlotLine("frametime line", tc.data.data(), tc.data.size());
            ImPlot::EndPlot();
        }
    }
    ImGui::EndChild();
    ImGui::PopID();
}

HudLayout ImGuiCtx::build_layout(hudTable* table, Font* fonts) {
    HudLayout L{};
    L.cols = table->cols;

    L.max_value_w.assign(L.cols, 0.0f);
    L.max_unit_w.assign(L.cols, 0.0f);
    L.col_content_w.assign(L.cols, 0.0f);

    float max_col0_w = 0.0f;

    for (const auto& row : table->rows) {
        if (!row.empty() && row[0].has_value()) {
            const Cell& v0 = *row[0];
            if (const auto* tc0 = std::get_if<TextCell>(&v0)) {
                float w = 0.0f;
                const float text_size = text_font_size(*table, *tc0);

                ImGui::PushFont(fonts->get(text_size));
                w += ImGui::CalcTextSize(tc0->text.c_str()).x;
                ImGui::PopFont();

                if (!tc0->unit.empty()) {
                    w += unit_gap;

                    if (tc0->unit == "%") {
                        ImGui::PushFont(fonts->get(text_size));
                        w += ImGui::CalcTextSize(tc0->unit.c_str()).x;
                        ImGui::PopFont();
                    } else {
                        ImGui::PushFont(fonts->get(unit_font_size(*table, *tc0)));
                        w += ImGui::CalcTextSize(tc0->unit.c_str()).x;
                        ImGui::PopFont();
                    }
                }

                if (w > max_col0_w)
                    max_col0_w = w;
            }
        }

        // Other columns: track maximum unit width per column
        for (int c = 0; c < L.cols && c < (int)row.size(); c++) {
            const auto& opt = row[c];
            if (!opt)
                continue;

            const Cell& v = *opt;
            const auto* tc = std::get_if<TextCell>(&v);
            if (!tc)
                continue;

            const ImVec2 value_sz = text_size(*table, *tc, fonts);
            if (value_sz.x > L.max_value_w[c])
                L.max_value_w[c] = value_sz.x;

            if (tc->unit.empty())
                continue;

            float uw = 0.0f;
            if (tc->unit == "%") {
                ImGui::PushFont(fonts->get(text_font_size(*table, *tc)));
                uw = ImGui::CalcTextSize(tc->unit.c_str()).x;
                ImGui::PopFont();
            } else {
                ImGui::PushFont(fonts->get(unit_font_size(*table, *tc)));
                uw = ImGui::CalcTextSize(tc->unit.c_str()).x;
                ImGui::PopFont();
            }

            if (uw > L.max_unit_w[c])
                L.max_unit_w[c] = uw;
        }
    }

    for (int c = 0; c < L.cols; c++) {
        if (c == 0) {
            L.col_content_w[c] = max_col0_w;
        } else {
            const bool has_units = (L.max_unit_w[c] > 0.0f);
            L.col_content_w[c] = L.max_value_w[c] + (has_units ? (unit_gap + L.max_unit_w[c]) : 0.0f);
        }
    }

    return L;
}

bool ImGuiCtx::draw(clientRes* r, slot_t* buf, Backend backend) {
    std::unique_lock<std::mutex> imgui_lock(m);
    std::unique_lock<std::mutex> vk_lock;
    ImGuiContext* imgui = nullptr;
    ImPlotContext* implot = nullptr;
    Font* fonts = nullptr;
    if (backend == Backend::VULKAN) {
        if (!vk) SPDLOG_ERROR("vulkan backend is not initalized");
        vk_lock = std::unique_lock<std::mutex>(vk->mutex());
        imgui = vk->imgui;
        implot = vk->implot;
        fonts = vk->fonts.get();
    } else if (backend == Backend::EGL) {
        if (!egl) SPDLOG_ERROR("egl backend is not initalized");
        imgui = egl->imgui;
        implot = egl->implot;
        fonts = egl->fonts.get();
    }

    hudTable local_table;
    {
        std::lock_guard lock(r->table_m);
        local_table = *r->table;
    }
    ImGui::SetCurrentContext(imgui);
    ImPlot::SetCurrentContext(implot);
    prepare_table_fonts(local_table, fonts);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {float(r->w), float(r->h)};


    ImGui::NewFrame();
    if (backend == Backend::EGL)
        ImGui_ImplOpenGL3_NewFrame();

    ImGui::SetNextWindowSize({float(r->w), float(r->h)});
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.5f);

    auto& style = ImGui::GetStyle();

    ImVec4 white = ImVec4(1, 1, 1, 1);

    ImGui::PushFont(fonts->get(local_table.font_size));
    ralign_width = ImGui::CalcTextSize("00000").x;
    ImGui::PopFont();

    ImGuiWindowFlags w_flags = ImGuiWindowFlags_NoDecoration;
    ImGui::Begin("HUD", nullptr, w_flags);
    HudLayout layout = build_layout(&local_table, fonts);

    const int cols = local_table.cols;
    if (cols > 0 && ImGui::BeginTable("overlay", cols, ImGuiTableFlags_NoClip | ImGuiTableFlags_SizingFixedFit)) {
        for (int c = 0; c < cols; c++)
            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, layout.col_content_w[c]);

        for (auto& row : local_table.rows) {
            const float row_h = row_height(local_table, row, fonts);
            ImGui::TableNextRow();
            for (int c = 0; c < cols; c++) {
                ImGui::TableSetColumnIndex(c);

                if (c >= (int)row.size()) {
                    continue;
                }
                auto& opt = row[c];
                if (!opt) {
                    continue;
                }

                Cell& cell = *opt;
                auto* tc = std::get_if<TextCell>(&cell);
                if (!tc) {
                    continue;
                }

                if (!tc->data.empty()) {
                    ImGui::Dummy({0, style.ItemSpacing.y});
                    ImGui::TableNextRow();
                    draw_graph_header(*tc, fonts, local_table);

                    ImGui::TableNextRow();
                    draw_graph_plot(*tc);
                    continue;
                }

                draw_value_with_unit(c, *tc, white, layout, fonts, local_table, row_h);
            }
        }

        ImGui::EndTable();
    }

    uint32_t w = calculate_width(layout);
    uint32_t h = ImGui::GetCursorPosY() + style.WindowPadding.y +
                 style.ItemSpacing.y + style.CellPadding.y;

    ImGui::End();
    ImGui::Render();

    if (w != r->w || h != r->h) {
        SPDLOG_DEBUG("resizing image from: {} {} to {} {}", r->w, r->h, w, h);
        r->w = w;
        r->h = h;
        return false;
    }

    if (backend == Backend::VULKAN)
        vk->record_cmd(*buf, w, h);

    if (backend == Backend::EGL)
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return true;
}

void ImGuiCtx::RenderOutlinedText(ImVec4 textColor, const char* text) {
    if (!text || !text[0]) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();

    float fontSize = ImGui::GetFontSize();

    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImU32 tc = ImGui::ColorConvertFloat4ToU32(textColor);

    float t = outline_padding_x;
    ImU32 oc = ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 1));

    dl->AddText(font, fontSize, ImVec2(pos.x - t, pos.y),     oc, text);
    dl->AddText(font, fontSize, ImVec2(pos.x + t, pos.y),     oc, text);
    dl->AddText(font, fontSize, ImVec2(pos.x,     pos.y - t), oc, text);
    dl->AddText(font, fontSize, ImVec2(pos.x,     pos.y + t), oc, text);

    dl->AddText(font, fontSize, pos, tc, text);

    ImVec2 sz = ImGui::CalcTextSize(text);
    ImGui::Dummy({sz.x, sz.y});
}

void ImGuiCtx::teardown() {
    vk.reset();
    egl.reset();
}
