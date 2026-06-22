#include <deque>
#include <mutex>
#include <imgui.h>
#include "implot.h"
#include <backends/imgui_impl_opengl3.h>

#include "imgui_ctx.h"
#include "font/font.h"
#include "../colors.h"
#include "../shared.h"

class ImGuiEGL {
public:
    ImGuiContext* imgui;
    ImPlotContext* implot;
    ImGuiCtx* imgui_ctx;
    std::shared_ptr<Font> fonts;

    ImGuiEGL(ImGuiCtx* imgui_ctx_) : imgui_ctx(imgui_ctx_) {
        std::lock_guard lock(imgui_ctx->m);
        IMGUI_CHECKVERSION();
        imgui = ImGui::CreateContext();
        ImGui::SetCurrentContext(imgui);
        implot = ImPlot::CreateContext();
        ImPlot::SetCurrentContext(implot);
        ImGui::StyleColorsDark();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.BackendPlatformName = "headless";
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

        auto& style = ImGui::GetStyle();
        style.CellPadding = ImVec2(5.f, 0.f);
        style.WindowBorderSize = 0.f;
        style.WindowMinSize = ImVec2(4, 4);

        ImGui_ImplOpenGL3_Init("#version 130");
        fonts = std::make_shared<Font>([] {
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            ImGui_ImplOpenGL3_CreateFontsTexture();
        });
    }

    ~ImGuiEGL() {
        std::lock_guard lock(imgui_ctx->m);
        if (imgui) {
            ImGui::SetCurrentContext(imgui);
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext(imgui);
            imgui = nullptr;
        }

        if (implot) {
            ImPlot::SetCurrentContext(implot);
            ImPlot::DestroyContext(implot);
            implot = nullptr;
        }
    }

private:
    std::mutex m;
};
