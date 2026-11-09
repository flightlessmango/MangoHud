#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_ctx.h"
#include "hud_layout.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include "vk.h"
#include "egl.h"

#include <algorithm>
#include <cmath>

static void draw_text(const HudText& run, ImVec2 origin) {
    if (run.text.empty())
        return;
    auto& draw = *ImGui::GetWindowDrawList();
    const auto pos = origin + run.pos;
    const auto* begin = run.text.data();
    const auto* end = begin + run.text.size();
    const auto outline = IM_COL32(0, 0, 0, 255);
    for (const auto offset : {ImVec2{-hud_outline, 0}, ImVec2{hud_outline, 0},
                              ImVec2{0, -hud_outline}, ImVec2{0, hud_outline}})
        draw.AddText(run.font, run.font_size, pos + offset, outline, begin, end);
    draw.AddText(run.font, run.font_size, pos, ImGui::ColorConvertFloat4ToU32(run.color), begin,
                 end);
}

static void draw_graph(const TextCell& cell, const ImRect& content, ImVec2 origin) {
    ImGui::SetCursorScreenPos(origin + content.Min);
    ImGui::PushID(&cell);
    const ImVec2 size{content.GetWidth(), std::min(content.GetHeight(), 50.0f)};
    if (size.x > 0.0f && size.y > 0.0f) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        if (ImGui::BeginChild("graph", size, false,
                              ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground)) {
            ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, {0, 0});
            ImPlot::PushStyleColor(ImPlotCol_PlotBg, {0, 0, 0, 0});
            ImPlot::PushStyleColor(ImPlotCol_FrameBg, {0, 0, 0, 0});
            if (ImPlot::BeginPlot("frametimes", size,
                                  ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs)) {
                ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations,
                                  ImPlotAxisFlags_NoDecorations);
                ImPlot::SetupAxisScale(
                    ImAxis_Y1, [](double v, void*) { return std::min(v, 49.9); },
                    [](double v, void*) { return v; });
                ImPlot::SetupAxesLimits(0, 200, 0, 50, ImGuiCond_Always);
                ImPlot::SetNextLineStyle({0, 1, 0, 1}, 2);
                ImPlot::PlotLine("frametime", cell.data.data(), cell.data.size());
                ImPlot::EndPlot();
            }
            ImPlot::PopStyleColor(2);
            ImPlot::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    ImGui::PopID();
}

static void draw_content(const HudLayout& layout, ImVec2 origin) {
    auto& draw = *ImGui::GetWindowDrawList();
    for (const auto& row : layout.rows)
        for (const auto& cell : row.cells) {
            if (const auto* progress = std::get_if<ProgressCell>(cell.cell)) {
                auto bar = cell.progress;
                bar.Translate(origin);
                const float range = progress->max_value - progress->min_value;
                const float fraction =
                    range == 0.0f
                        ? 0.0f
                        : std::clamp((progress->value - progress->min_value) / range, 0.0f, 1.0f);
                bar.Max.x = bar.Min.x + bar.GetWidth() * fraction;
                if (bar.GetWidth() > 0.0f && bar.GetHeight() > 0.0f)
                    draw.AddRectFilled(bar.Min, bar.Max,
                                       ImGui::ColorConvertFloat4ToU32(progress->vec));
            }
            for (const auto& run : cell.text)
                draw_text(run, origin);
            if (cell.nested)
                draw_content(*cell.nested, origin + cell.content.Min);
            if (const auto* text = std::get_if<TextCell>(cell.cell); text && !text->data.empty())
                draw_graph(*text, cell.content, origin);
            if (layout.debug) {
                draw.AddRect(cell.bounds.Min + origin, cell.bounds.Max + origin,
                             IM_COL32(255, 220, 0, 255));
                draw.AddRect(cell.content.Min + origin, cell.content.Max + origin,
                             IM_COL32(0, 220, 255, 255));
            }
        }
}

static void draw_separators(const HudLayout& layout, ImVec2 origin) {
    auto& draw = *ImGui::GetWindowDrawList();
    for (const auto& row : layout.rows)
        for (const auto& cell : row.cells) {
            if (cell.nested)
                draw_separators(*cell.nested, origin + cell.content.Min);
            if (cell.separator_text)
                draw_text(*cell.separator_text, origin);
            if (!cell.separator)
                continue;
            const auto& line = *cell.separator;
            const auto& style = hud_cell_style(*cell.cell);
            draw.AddLine(origin + line.Min, origin + line.Max, IM_COL32(0, 0, 0, 255),
                         style.separator_thickness + hud_outline * 2.0f);
            draw.AddLine(origin + line.Min, origin + line.Max,
                         ImGui::ColorConvertFloat4ToU32(style.separator_vec),
                         style.separator_thickness);
        }
}

static void fill_background(ImRect bounds, const ImRect& clip, ImVec4 color) {
    bounds.ClipWith(clip);
    if (color.w > 0.0f && bounds.GetWidth() > 0.0f && bounds.GetHeight() > 0.0f) {
        const auto origin = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddRectFilled(bounds.Min + origin, bounds.Max + origin,
                                                  ImGui::ColorConvertFloat4ToU32(color));
    }
}

static void draw_backgrounds(const HudLayout& layout, ImVec2 origin, ImVec4 inherited,
                             const ImRect& clip, bool fill_rows = false) {
    for (const auto& row : layout.rows) {
        auto strip = row.bounds;
        if (fill_rows)
            strip.Max.x = layout.size.x;
        strip.Translate(origin);
        strip.ClipWith(clip);
        if (strip.GetWidth() <= 0.0f || strip.GetHeight() <= 0.0f)
            continue;
        float x = strip.Min.x;
        for (const auto& cell : row.cells) {
            auto bounds = cell.bounds;
            bounds.Translate(origin);
            if (bounds.Min.x >= strip.Max.x)
                break;
            fill_background({{x, strip.Min.y}, {bounds.Min.x, strip.Max.y}}, strip, inherited);
            fill_background({{bounds.Min.x, bounds.Max.y}, {bounds.Max.x, strip.Max.y}}, strip,
                            inherited);
            const auto& style = hud_cell_style(*cell.cell);
            auto color = style.background_color.empty() ? inherited : style.background_vec;
            if (const auto* progress = std::get_if<ProgressCell>(cell.cell))
                color = progress->background_vec;
            if (cell.nested && cell.nested->size.x > 0.0f && cell.nested->size.y > 0.0f) {
                const ImRect inner(origin + cell.content.Min,
                                   origin + cell.content.Min + cell.nested->size);
                fill_background({bounds.Min, {bounds.Max.x, inner.Min.y}}, strip, color);
                fill_background({{bounds.Min.x, inner.Max.y}, bounds.Max}, strip, color);
                fill_background({{bounds.Min.x, inner.Min.y}, {inner.Min.x, inner.Max.y}}, strip,
                                color);
                fill_background({{inner.Max.x, inner.Min.y}, {bounds.Max.x, inner.Max.y}}, strip,
                                color);
                auto child_clip = inner;
                child_clip.ClipWith(strip);
                draw_backgrounds(*cell.nested, inner.Min, color, child_clip, true);
            } else
                fill_background(bounds, strip, color);
            x = bounds.Max.x;
        }
        fill_background({{x, strip.Min.y}, strip.Max}, strip, inherited);
    }
}

void ImGuiCtx::init_vk(VkCtx* vk_) {
    if (!vk)
        vk = std::make_shared<ImGuiVK>(vk_, this);
}

void ImGuiCtx::init_egl() {
    if (!egl)
        egl = std::make_shared<ImGuiEGL>(this);
}

bool ImGuiCtx::draw(uint32_t w, uint32_t h, Resolution& size, BufferSet* buf, Backend backend,
                    std::shared_ptr<HudConfig> hud, std::mutex& hud_m) {
    size = {w, h};

    std::unique_lock<std::mutex> global_lock(global_m);
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
        std::lock_guard lock(hud_m);
        windows = hud->windows;
    }
    ImGui::SetCurrentContext(imgui);
    ImPlot::SetCurrentContext(implot);
    for (auto& window : windows)
        prepare_hud_fonts(window.table, *fonts);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {float(w), float(h)};

    ImGui::NewFrame();
    if (backend == Backend::EGL)
        ImGui_ImplOpenGL3_NewFrame();

    float max_x = 0.0f;
    float max_y = 0.0f;
    for (std::size_t i = 0; i < windows.size(); i++) {
        HudWindow& window = windows[i];
        const auto layout = layout_hud(window.table, *fonts);
        const uint32_t window_w = std::ceil(layout.size.x);
        const uint32_t window_h = std::ceil(layout.size.y);
        const std::string name = "HUD##" + std::to_string(i);
        ImGui::SetNextWindowSize({float(window_w), float(window_h)});
        ImGui::SetNextWindowPos(window.position, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::Begin(name.c_str(), nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
        const auto origin = ImGui::GetWindowPos();
        auto background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        background.w = window.background ? 0.5f : 0.0f;
        draw_backgrounds(layout, {}, background, ImRect({}, layout.size));
        draw_content(layout, origin);
        draw_separators(layout, origin);
        ImGui::End();
        ImGui::PopStyleVar(2);

        max_x = std::max(max_x, window.position.x + (float)window_w);
        max_y = std::max(max_y, window.position.y + (float)window_h);
    }

    size = {(uint32_t)std::ceil(max_x), (uint32_t)std::ceil(max_y)};

    ImGui::Render();

    if (size != Resolution{w, h})
        return false;

    if (backend == Backend::VULKAN)
        return vk->record_cmd(*buf, w, h);

    if (backend == Backend::EGL)
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return true;
}

ImGuiCtx::~ImGuiCtx() {
    vk.reset();
    egl.reset();
}
