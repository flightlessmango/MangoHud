#include <deque>
#include <unistd.h>
#include <math.h>
#include "backends/imgui_impl_opengl3.h"
#include "imgui_ctx.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "font/font.h"
#include "../server/config.h"
#include "vk.h"
#include "egl.h"
std::mutex init_m;
static constexpr float unit_gap = -1.5f;
static constexpr float hud_cell_padding_x = 4.0f;
static constexpr float hud_cell_padding_y = 2.0f;
static constexpr float hud_col_gap = 16.0f;
static constexpr float hud_row_gap = 6.0f;
static constexpr float outline_padding_x = 1.5f;

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

uint32_t ImGuiCtx::calculate_width(const HudLayout& L, const HudWindow& window) {
    float total = L.content_size.x;

    total += window.padding * 2.0f;
    total += std::ceil(outline_padding_x) * 2.0f;

    return (uint32_t)std::ceil(total);
}

uint32_t ImGuiCtx::calculate_height(const HudLayout& L, const HudWindow& window) {
    float total = L.content_size.y;

    total += window.padding * 2.0f;

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

static float style_font_size(const hudTable& table, const CellStyle& style) {
    if (style.font_size > 0.0f)
        return style.font_size;

    return table.font_size * style.font_scale;
}

static float unit_font_size(const hudTable& table, const TextCell& tc) {
    return style_font_size(table, tc.style) / 2.0f;
}

static void prepare_table_fonts(const hudTable& table, Font* fonts) {
    fonts->get(table.font_size);
    fonts->get(table.font_size / 2.0f);

    for (const auto& row : table.rows) {
        for (const auto& opt : row) {
            if (!opt)
                continue;

            const auto* tc = std::get_if<TextCell>(&*opt);
            if (tc) {
                fonts->get(style_font_size(table, tc->style));
                fonts->get(unit_font_size(table, *tc));
                continue;
            }

            const auto* pc = std::get_if<ProgressCell>(&*opt);
            if (pc) {
                fonts->get(style_font_size(table, pc->style));
                continue;
            }

            const auto* nested = std::get_if<TableCell>(&*opt);
            if (nested && nested->table)
                prepare_table_fonts(*nested->table, fonts);
        }
    }
}

static ImVec2 outlined_text_size_current_font(const char* text) {
    ImVec2 size = ImGui::CalcTextSize(text);
    size.x += std::ceil(outline_padding_x) * 2.0f;
    size.y += std::ceil(outline_padding_x) * 2.0f;
    return size;
}

static ImVec2 raw_text_size(const hudTable& table, const CellStyle& style, const std::string& text, Font* fonts) {
    if (text.empty())
        return {};

    ImGui::PushFont(fonts->get(style_font_size(table, style)));
    ImVec2 size = ImGui::CalcTextSize(text.c_str());
    ImGui::PopFont();
    return size;
}

static ImVec2 text_size(const hudTable& table, const CellStyle& style, const std::string& text, Font* fonts) {
    if (text.empty())
        return {};

    ImGui::PushFont(fonts->get(style_font_size(table, style)));
    ImVec2 size = outlined_text_size_current_font(text.c_str());
    ImGui::PopFont();
    return size;
}

static float text_left_bearing(const char* text, ImFont* font, float size) {
    if (!text || !text[0])
        return 0.0f;

    unsigned int c = 0;
    if (ImTextCharFromUtf8(&c, text, nullptr) <= 0)
        return 0.0f;

    const ImFontGlyph* glyph = font->FindGlyph((ImWchar)c);
    if (!glyph)
        return 0.0f;

    return glyph->X0 * (size / font->FontSize);
}

struct TextYBounds {
    float min = 0.0f;
    float max = 0.0f;
};

struct TextXBounds {
    float min = 0.0f;
    float max = 0.0f;
};

static TextXBounds text_x_bounds(const char* text, ImFont* font, float size) {
    TextXBounds bounds;
    if (!text || !text[0])
        return bounds;

    const float scale = size / font->FontSize;
    float x = 0.0f;
    bool found = false;
    const char* s = text;
    while (*s) {
        unsigned int c = 0;
        const int bytes = ImTextCharFromUtf8(&c, s, nullptr);
        if (bytes <= 0)
            break;
        s += bytes;

        const ImFontGlyph* glyph = font->FindGlyph((ImWchar)c);
        if (!glyph)
            continue;

        if (glyph->Visible) {
            const float x0 = x + glyph->X0 * scale;
            const float x1 = x + glyph->X1 * scale;
            if (!found) {
                bounds.min = x0;
                bounds.max = x1;
                found = true;
            } else {
                bounds.min = std::min(bounds.min, x0);
                bounds.max = std::max(bounds.max, x1);
            }
        }

        x += glyph->AdvanceX * scale;
    }

    if (!found) {
        bounds.min = 0.0f;
        bounds.max = ImGui::CalcTextSize(text).x;
    }

    return bounds;
}

static TextYBounds text_y_bounds(const char* text, ImFont* font, float size) {
    TextYBounds bounds;
    if (!text || !text[0])
        return bounds;

    const float scale = size / font->FontSize;
    bool found = false;
    const char* s = text;
    while (*s) {
        unsigned int c = 0;
        const int bytes = ImTextCharFromUtf8(&c, s, nullptr);
        if (bytes <= 0)
            break;
        s += bytes;

        if (c < 32)
            continue;

        const ImFontGlyph* glyph = font->FindGlyph((ImWchar)c);
        if (!glyph || !glyph->Visible)
            continue;

        const float y0 = glyph->Y0 * scale;
        const float y1 = glyph->Y1 * scale;
        if (!found) {
            bounds.min = y0;
            bounds.max = y1;
            found = true;
        } else {
            bounds.min = std::min(bounds.min, y0);
            bounds.max = std::max(bounds.max, y1);
        }
    }

    if (!found) {
        bounds.min = 0.0f;
        bounds.max = size;
    }

    return bounds;
}

static TextYBounds text_y_bounds(const hudTable& table, const TextCell& tc, Font* fonts) {
    const float size = style_font_size(table, tc.style);
    return text_y_bounds(tc.text.c_str(), fonts->get(size), size);
}

static TextYBounds unit_y_bounds(const hudTable& table, const TextCell& tc, Font* fonts) {
    if (tc.unit.empty())
        return {};

    const float size = tc.unit == "%" ? style_font_size(table, tc.style) : unit_font_size(table, tc);
    return text_y_bounds(tc.unit.c_str(), fonts->get(size), size);
}

static ImVec2 reserved_value_size(const hudTable& table, const TextCell& tc, Font* fonts) {
    if (tc.unit.empty())
        return {};

    ImGui::PushFont(fonts->get(style_font_size(table, tc.style)));
    ImVec2 size = outlined_text_size_current_font((tc.unit == "%" || tc.unit == "W" || tc.unit == "GiB") ? "100" : "00000");
    ImGui::PopFont();
    return size;
}

static float cell_height(const hudTable& table, const TextCell& tc, Font* fonts) {
    const TextYBounds value = text_y_bounds(table, tc, fonts);
    const TextYBounds unit = unit_y_bounds(table, tc, fonts);
    return std::max(value.max - value.min, unit.max - unit.min) + std::ceil(outline_padding_x) * 2.0f;
}

static float graph_height(const hudTable& table, const TextCell& tc, Font* fonts) {
    ImGui::PushFont(fonts->get(unit_font_size(table, tc)));
    const float header_h = outlined_text_size_current_font("frametime").y;
    ImGui::PopFont();

    return hud_row_gap + header_h + 50.0f;
}

static float progress_height(const hudTable& table, const ProgressCell& pc, Font* fonts) {
    const std::string& text = pc.layout_text.empty() ? pc.text : pc.layout_text;
    if (text.empty())
        return std::max(6.0f, table.font_size * 0.6f);

    const float size = style_font_size(table, pc.style);
    ImFont* font = fonts->get(size);
    const TextYBounds bounds = text_y_bounds(text.c_str(), font, size);
    return bounds.max - bounds.min + std::ceil(outline_padding_x) * 2.0f;
}

static HudLayout build_table_layout(hudTable* table, Font* fonts);

static int cell_colspan(const Cell& cell) {
    return std::visit([](const auto& c) {
        return std::max(1, c.style.colspan);
    }, cell);
}

static float spanned_width(const HudLayout& L, int start_col, int colspan) {
    if (start_col < 0 || start_col >= L.cols)
        return 0.0f;

    const int end_col = std::min(L.cols - 1, start_col + std::max(1, colspan) - 1);
    const HudBox& start = L.col_boxes[start_col];
    const HudBox& end = L.col_boxes[end_col];
    return std::max(0.0f, end.pos.x + end.size.x - start.pos.x);
}

static ImVec2 nested_table_size(hudTable& table, Font* fonts) {
    HudLayout layout = build_table_layout(&table, fonts);
    return {
        layout.content_size.x + hud_cell_padding_x,
        layout.content_size.y + hud_cell_padding_y,
    };
}

static float row_height(hudTable& table, const std::vector<MaybeCell>& row, Font* fonts) {
    float height = 0.0f;

    for (const auto& opt : row) {
        if (!opt)
            continue;

        const auto* tc = std::get_if<TextCell>(&*opt);
        if (!tc) {
            const auto* pc = std::get_if<ProgressCell>(&*opt);
            if (pc) {
                height = std::max(height, progress_height(table, *pc, fonts));
                continue;
            }

            const auto* nested = std::get_if<TableCell>(&*opt);
            if (nested && nested->table) {
                height = std::max(height, nested_table_size(*nested->table, fonts).y);
            }
            continue;
        }

        if (!tc->data.empty()) {
            height = std::max(height, graph_height(table, *tc, fonts));
            continue;
        }

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
                                   float row_h,
                                   float cell_w,
                                   bool numeric_align) {
    const ImVec2 base = ImGui::GetCursorPos();
    const float text_font_sz = style_font_size(table, tc.style);
    const ImVec2 value_sz = raw_text_size(table, tc.style, tc.text, fonts);
    const TextYBounds value_y = text_y_bounds(table, tc, fonts);
    const TextYBounds unit_y = unit_y_bounds(table, tc, fonts);
    const float outline_pad = std::ceil(outline_padding_x);
    const float text_y = base.y + row_h - outline_pad - value_y.max;
    const float value_top_y = text_y + value_y.min;
    const float unit_pos_y = value_top_y - unit_y.min;

    if (tc.style.align != CellAlign::Default && tc.unit.empty()) {
        ImFont* font = fonts->get(text_font_sz);
        const TextXBounds x_bounds = text_x_bounds(tc.text.c_str(), font, text_font_sz);
        const float visual_w = x_bounds.max - x_bounds.min;
        float text_x = base.x - x_bounds.min;

        if (tc.style.align == CellAlign::Center)
            text_x = base.x + (cell_w - visual_w) * 0.5f - x_bounds.min;
        else if (tc.style.align == CellAlign::Right)
            text_x = base.x + cell_w - std::ceil(outline_padding_x) - x_bounds.max;

        ImGui::SetCursorPos(ImVec2(text_x, text_y));
        ImGui::PushFont(font);
        RenderOutlinedText(tc.vec, tc.text.c_str());
        ImGui::PopFont();
        ImGui::SetCursorPos(ImVec2(base.x, base.y + row_h));
        return;
    }

    if (col_index == 0 && !numeric_align) {
        ImFont* font = fonts->get(text_font_sz);
        const float text_x = base.x - text_left_bearing(tc.text.c_str(), font, text_font_sz);
        ImGui::SetCursorPos(ImVec2(text_x, text_y));
        ImGui::PushFont(font);
        RenderOutlinedText(tc.vec, tc.text.c_str());
        ImGui::PopFont();
        if (!tc.unit.empty()) {
            ImGui::SetCursorPos(ImVec2(text_x + value_sz.x + unit_gap, unit_pos_y));

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
    if (cell_w <= 0.0f)
        cell_w = ImGui::GetContentRegionAvail().x;

    const float unit_start_x = cell_left + (cell_w - outline_pad - L.max_unit_w[col_index]);
    const float value_right_x = unit_start_x - unit_gap;
    const float value_w = L.max_value_w[col_index];
    const float value_left_x = value_right_x - value_w;

    ImFont* value_font = fonts->get(text_font_sz);
    const float value_x = value_left_x + value_w - outline_pad - value_sz.x - text_left_bearing(tc.text.c_str(), value_font, text_font_sz);
    ImGui::SetCursorPos(ImVec2(value_x, text_y));
    ImGui::PushFont(value_font);
    RenderOutlinedText(tc.vec, tc.text.c_str());
    ImGui::PopFont();

    if (!tc.unit.empty()) {
        ImGui::SetCursorPos(ImVec2(unit_start_x + outline_pad, unit_pos_y));
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

void ImGuiCtx::draw_graph_header(const TextCell& tc, Font* fonts, const hudTable& table, const HudLayout& L) {
    const ImVec2 base = ImGui::GetCursorPos();
    const float content_w = std::max(0.0f, L.content_size.x - hud_cell_padding_x * 2.0f);
    ImGui::PushFont(fonts->get(unit_font_size(table, tc)));
    RenderOutlinedText(colors.get("eb5b5b"), "frametime");

    float max = *std::max_element(tc.data.begin(), tc.data.end());
    float min = *std::min_element(tc.data.begin(), tc.data.end());
    ImGui::SetCursorPos(ImVec2(base.x + content_w - ralign_width, base.y));
    right_aligned(colors.get("FFFFFF"), ralign_width, "min: %.1fms, max: %.1fms", min, max);
    ImGui::PopFont();
}

void ImGuiCtx::draw_graph_plot(const TextCell& tc, float width) {
    ImGui::PushID(&tc);
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

void ImGuiCtx::draw_progress_bar(const ProgressCell& pc, Font* fonts, const hudTable& table, float width, float height) {
    const ImVec2 local_pos = ImGui::GetCursorPos();
    const float bar_h = progress_height(table, pc, fonts);
    float bar_y = local_pos.y + (height - bar_h) * 0.5f;
    float text_x = 0.0f;
    float text_y = 0.0f;
    ImFont* text_font = nullptr;

    if (!pc.text.empty()) {
        const float font_size = style_font_size(table, pc.style);
        text_font = fonts->get(font_size);
        ImGui::PushFont(text_font);
        const ImVec2 text_sz = ImGui::CalcTextSize(pc.text.c_str());
        ImGui::PopFont();
        const float outline_pad = std::ceil(outline_padding_x);
        const float outlined_text_w = text_sz.x + outline_pad * 2.0f;
        const TextYBounds bounds = text_y_bounds(pc.text.c_str(), text_font, font_size);
        const float text_h = bounds.max - bounds.min + outline_pad * 2.0f;

        text_x = local_pos.x + (width - outlined_text_w) * 0.5f + outline_pad;
        text_y = local_pos.y + height - outline_pad - bounds.max;
        bar_y = text_y + bounds.min - outline_pad + (text_h - bar_h) * 0.5f;
    }

    ImGui::SetCursorPos(ImVec2(local_pos.x, bar_y));
    const ImVec2 screen_pos = ImGui::GetCursorScreenPos();
    const float range = pc.max_value - pc.min_value;
    float fraction = range == 0.0f ? 0.0f : (pc.value - pc.min_value) / range;
    fraction = std::max(0.0f, std::min(1.0f, fraction));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 end = ImVec2(screen_pos.x + width, screen_pos.y + bar_h);
    const ImVec2 fill_end = ImVec2(screen_pos.x + width * fraction, screen_pos.y + bar_h);
    draw_list->AddRectFilled(screen_pos, end, ImGui::ColorConvertFloat4ToU32(pc.background_vec));
    draw_list->AddRectFilled(screen_pos, fill_end, ImGui::ColorConvertFloat4ToU32(pc.vec));

    if (!pc.text.empty()) {
        ImGui::SetCursorPos(ImVec2(text_x, text_y));
        ImGui::PushFont(text_font);
        RenderOutlinedText(ImVec4(1, 1, 1, 1), pc.text.c_str());
        ImGui::PopFont();
    }

    ImGui::SetCursorPos(ImVec2(local_pos.x, local_pos.y + height));
}

static HudLayout build_table_layout(hudTable* table, Font* fonts) {
    HudLayout L{};
    L.cols = table->cols;
    L.max_value_w.assign(L.cols, 0.0f);
    L.max_unit_w.assign(L.cols, 0.0f);
    L.col_boxes.resize(L.cols);

    float max_col0_w = 0.0f;

    for (const auto& row : table->rows) {
        if (!row.empty() && row[0].has_value()) {
            const Cell& v0 = *row[0];
            if (const auto* tc0 = std::get_if<TextCell>(&v0)) {
                float w = 0.0f;
                const float text_size = style_font_size(*table, tc0->style);

                ImGui::PushFont(fonts->get(text_size));
                w += outlined_text_size_current_font(tc0->text.c_str()).x;
                ImGui::PopFont();

                if (!tc0->unit.empty()) {
                    w += unit_gap;

                    if (tc0->unit == "%") {
                        ImGui::PushFont(fonts->get(text_size));
                        w += outlined_text_size_current_font(tc0->unit.c_str()).x;
                        ImGui::PopFont();
                    } else {
                        ImGui::PushFont(fonts->get(unit_font_size(*table, *tc0)));
                        w += outlined_text_size_current_font(tc0->unit.c_str()).x;
                        ImGui::PopFont();
                    }
                }

                if (w > max_col0_w)
                    max_col0_w = w;
            } else if (const auto* pc0 = std::get_if<ProgressCell>(&v0)) {
                const std::string& text = pc0->layout_text.empty() ? pc0->text : pc0->layout_text;
                const float w = std::max(100.0f, text_size(*table, pc0->style, text, fonts).x);
                if (w > max_col0_w)
                    max_col0_w = w;
            } else if (const auto* nested = std::get_if<TableCell>(&v0); nested && nested->table) {
                const ImVec2 size = nested_table_size(*nested->table, fonts);
                if (size.x > max_col0_w)
                    max_col0_w = size.x;
            }
        }

        // Other columns: track maximum unit width per column
        for (int c = 0; c < L.cols && c < (int)row.size(); c++) {
            const auto& opt = row[c];
            if (!opt)
                continue;

            const Cell& v = *opt;
            const auto* tc = std::get_if<TextCell>(&v);
            if (!tc) {
                const auto* pc = std::get_if<ProgressCell>(&v);
                if (pc) {
                    const std::string& text = pc->layout_text.empty() ? pc->text : pc->layout_text;
                    const float w = std::max(100.0f, text_size(*table, pc->style, text, fonts).x);
                    if (w > L.max_value_w[c])
                        L.max_value_w[c] = w;
                    continue;
                }

                const auto* nested = std::get_if<TableCell>(&v);
                if (nested && nested->table) {
                    const ImVec2 size = nested_table_size(*nested->table, fonts);
                    if (size.x > L.max_value_w[c])
                        L.max_value_w[c] = size.x;
                }
                continue;
            }

            const ImVec2 value_sz = text_size(*table, tc->style, tc->text, fonts);
            float value_w = value_sz.x;
            const ImVec2 reserved_sz = reserved_value_size(*table, *tc, fonts);
            value_w = std::max(value_w, reserved_sz.x);
            if (value_w > L.max_value_w[c])
                L.max_value_w[c] = value_w;

            if (tc->unit.empty())
                continue;

            float uw = 0.0f;
            if (tc->unit == "%") {
                ImGui::PushFont(fonts->get(style_font_size(*table, tc->style)));
                uw = outlined_text_size_current_font(tc->unit.c_str()).x;
                ImGui::PopFont();
            } else {
                ImGui::PushFont(fonts->get(unit_font_size(*table, *tc)));
                uw = outlined_text_size_current_font(tc->unit.c_str()).x;
                ImGui::PopFont();
            }

            if (uw > L.max_unit_w[c])
                L.max_unit_w[c] = uw;
        }
    }

    for (int c = 0; c < L.cols; c++) {
        if (c == 0) {
            L.col_boxes[c].size.x = max_col0_w;
        } else {
            const bool has_units = (L.max_unit_w[c] > 0.0f);
            L.col_boxes[c].size.x = L.max_value_w[c] + (has_units ? (unit_gap + L.max_unit_w[c]) : 0.0f);
        }
    }

    float x = 0.0f;
    for (int c = 0; c < L.cols; c++) {
        L.col_boxes[c].pos.x = x;
        x += L.col_boxes[c].size.x + hud_col_gap;
    }

    L.row_boxes.resize(table->rows.size());
    float y = 0.0f;
    for (std::size_t r = 0; r < table->rows.size(); r++) {
        L.row_boxes[r].pos.y = y;
        L.row_boxes[r].size.y = row_height(*table, table->rows[r], fonts);
        y += L.row_boxes[r].size.y + hud_row_gap;
    }

    for (const HudBox& col : L.col_boxes)
        if (col.size.x > 0.0f)
            L.content_size.x = std::max(L.content_size.x, col.pos.x + col.size.x);
    if (L.content_size.x > 0.0f)
        L.content_size.x += hud_cell_padding_x * 2.0f;
    if (!L.row_boxes.empty()) {
        const HudBox& last = L.row_boxes.back();
        L.content_size.y = hud_cell_padding_y + last.pos.y + last.size.y + hud_cell_padding_y;
    }

    return L;
}

HudLayout ImGuiCtx::build_layout(hudTable* table, Font* fonts) {
    return build_table_layout(table, fonts);
}

void ImGuiCtx::draw_table(hudTable& table, Font* fonts, const HudLayout& layout, bool first_col_numeric) {
    ImVec4 white = ImVec4(1, 1, 1, 1);

    ImGui::PushFont(fonts->get(table.font_size));
    ralign_width = ImGui::CalcTextSize("00000").x;
    ImGui::PopFont();

    const ImVec2 cursor_origin = ImGui::GetCursorPos();
    const ImVec2 origin = ImVec2(cursor_origin.x + std::ceil(outline_padding_x), cursor_origin.y);
    ImGui::PushID(&table);
    for (std::size_t r = 0; r < table.rows.size(); r++) {
        auto& row = table.rows[r];
        const float row_h = layout.row_boxes[r].size.y;
        const float row_y = origin.y + layout.row_boxes[r].pos.y + hud_cell_padding_y;

        for (int c = 0; c < layout.cols; c++) {
            if (c >= (int)row.size() || !row[c])
                continue;

            Cell& cell = *row[c];
            const int colspan = cell_colspan(cell);
            const float cell_x = origin.x + layout.col_boxes[c].pos.x + hud_cell_padding_x;
            const float cell_w = spanned_width(layout, c, colspan);
            ImGui::SetCursorPos(ImVec2(cell_x, row_y));

            if (auto* nested = std::get_if<TableCell>(&cell); nested && nested->table) {
                HudLayout nested_layout = build_table_layout(nested->table.get(), fonts);
                const float nested_y = row_y + hud_cell_padding_y;
                float nested_x = cell_x;
                if (c > 0) {
                    const float pad_r = std::ceil(outline_padding_x);
                    const float parent_unit_x = cell_x + cell_w - pad_r - layout.max_unit_w[c];
                    const float nested_unit_x = hud_cell_padding_x + nested_layout.col_boxes[0].size.x - pad_r - nested_layout.max_unit_w[0];
                    nested_x = parent_unit_x - nested_unit_x;
                }

                ImGui::SetCursorPos(ImVec2(nested_x, nested_y));
                draw_table(*nested->table, fonts, nested_layout, c > 0);
                continue;
            }

            if (auto* pc = std::get_if<ProgressCell>(&cell)) {
                draw_progress_bar(*pc, fonts, table, cell_w, row_h);
                continue;
            }

            auto* tc = std::get_if<TextCell>(&cell);
            if (!tc)
                continue;

            if (!tc->data.empty()) {
                ImGui::SetCursorPos(ImVec2(origin.x + hud_cell_padding_x, row_y));
                draw_graph_header(*tc, fonts, table, layout);

                ImGui::PushFont(fonts->get(unit_font_size(table, *tc)));
                const float header_h = outlined_text_size_current_font("frametime").y;
                ImGui::PopFont();

                ImGui::SetCursorPos(ImVec2(origin.x + hud_cell_padding_x, row_y + header_h + hud_row_gap));
                const float full_width = std::max(0.0f, layout.content_size.x - hud_cell_padding_x * 2.0f);
                draw_graph_plot(*tc, colspan > 1 ? cell_w : full_width);
                continue;
            }

            draw_value_with_unit(c, *tc, white, layout, fonts, table, row_h, cell_w, first_col_numeric && c == 0);
        }
    }
    ImGui::PopID();
    ImGui::SetCursorPos(ImVec2(cursor_origin.x, cursor_origin.y + layout.content_size.y));

}

void ImGuiCtx::begin_window(const HudWindow& window, ImVec2 size, const char* name) {
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowPos(window.position, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(window.background ? 0.5f : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(window.padding, window.padding));

    ImGuiWindowFlags w_flags = ImGuiWindowFlags_NoDecoration;
    ImGui::Begin(name, nullptr, w_flags);
}

void ImGuiCtx::end_window() {
    ImGui::End();
    ImGui::PopStyleVar();
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

    std::vector<HudWindow> windows;
    {
        std::lock_guard lock(r->hud_m);
        windows = r->hud->windows;
    }
    ImGui::SetCurrentContext(imgui);
    ImPlot::SetCurrentContext(implot);
    for (auto& window : windows)
        prepare_table_fonts(window.table, fonts);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {float(r->w), float(r->h)};


    ImGui::NewFrame();
    if (backend == Backend::EGL)
        ImGui_ImplOpenGL3_NewFrame();

    float max_x = 0.0f;
    float max_y = 0.0f;
    for (std::size_t i = 0; i < windows.size(); i++) {
        HudWindow& window = windows[i];
        HudLayout layout = build_layout(&window.table, fonts);
        const uint32_t window_w = calculate_width(layout, window);
        const uint32_t window_h = calculate_height(layout, window);
        const std::string name = "HUD##" + std::to_string(i);

        begin_window(window, {float(window_w), float(window_h)}, name.c_str());
        draw_table(window.table, fonts, layout);
        end_window();

        max_x = std::max(max_x, window.position.x + (float)window_w);
        max_y = std::max(max_y, window.position.y + (float)window_h);
    }

    uint32_t w = (uint32_t)std::ceil(max_x);
    uint32_t h = (uint32_t)std::ceil(max_y);

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

    ImVec2 sz = outlined_text_size_current_font(text);
    ImGui::Dummy({sz.x, sz.y});
}

void ImGuiCtx::teardown() {
    vk.reset();
    egl.reset();
}
