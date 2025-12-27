#pragma once

#include <imgui.h>

#ifdef __linux__
#include <implot.h>
#endif

struct imgui_contexts {
    ImGuiContext* imgui = nullptr;
#ifdef IMPLOT_API
    ImPlotContext* implot = nullptr;
#endif
};

static imgui_contexts create_imgui_contexts(ImFontAtlas* shared_font_atlas = NULL)
{
    imgui_contexts contexts;
    contexts.imgui = ImGui::CreateContext(shared_font_atlas);
#ifdef IMPLOT_API
    contexts.implot = ImPlot::CreateContext();
#endif
    return contexts;
}

static void destroy_imgui_contexts(imgui_contexts& contexts)
{
    ImGui::DestroyContext(contexts.imgui);
    contexts.imgui = nullptr;
#ifdef IMPLOT_API
    ImPlot::DestroyContext(contexts.implot);
    contexts.implot = nullptr;
#endif
}

static imgui_contexts get_current_imgui_contexts() {
    imgui_contexts saved_contexts;
    saved_contexts.imgui = ImGui::GetCurrentContext();
#ifdef IMPLOT_API
    saved_contexts.implot = ImPlot::GetCurrentContext();
#endif
    return saved_contexts;
}

static void make_imgui_contexts_current(imgui_contexts contexts)
{
    ImGui::SetCurrentContext(contexts.imgui);
#ifdef IMPLOT_API
    ImPlot::SetCurrentContext(contexts.implot);
#endif
}
