#include <deque>
#include <unistd.h>
#include <math.h>

#include "vulkan_ctx.h"
#include "imgui_ctx.h"
#include "imgui.h"
#include "font/font.h"
#include "../server/config.h"

ImGuiCtx::ImGuiCtx(VkDevice device_, VkPhysicalDevice phys_, VkInstance instance_,
                   VkQueue queue_, uint32_t queue_idx_, VkFormat fmt_) :
                   device(device_), physicalDevice(phys_), instance(instance_),
                   graphicsQueue(queue_), graphicsQueueIdx(queue_idx_), fmt(fmt_){
    init();
};

bool ImGuiCtx::init() {
    IMGUI_CHECKVERSION();
    imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui);
    implot = ImPlot::CreateContext();
    ImPlot::SetCurrentContext(implot);
    ImGui::StyleColorsDark();
    fonts = std::make_unique<Font>();

    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance = instance;
    ii.PhysicalDevice = physicalDevice;
    ii.Device = device;
    ii.QueueFamily = graphicsQueueIdx;
    ii.Queue = graphicsQueue;
    ii.DescriptorPool = create_desc_pool();

    ii.MinImageCount = 2;
    ii.ImageCount = 2;
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ii.UseDynamicRendering = true;
    ii.PipelineRenderingCreateInfo = VkPipelineRenderingCreateInfoKHR{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    ii.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    ii.PipelineRenderingCreateInfo.pColorAttachmentFormats = &fmt;

    create_cmd();
    ImGui_ImplVulkan_Init(&ii);
    ImGui_ImplVulkan_CreateFontsTexture();
    return true;
};

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

uint32_t ImGuiCtx::calculate_width(const hudTable& table, const HudLayout& L) {
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

void ImGuiCtx::draw_value_with_unit(int col_index,
                                   const TextCell& tc,
                                   const ImVec4& unit_col,
                                   const HudLayout& L) {
    const ImVec2 base = ImGui::GetCursorPos();

    auto unit_size = [&](const std::string& u) -> ImVec2 {
        if (u.empty()) {
            return ImVec2(0.0f, 0.0f);
        }
        if (u == "%") {
            ImGui::PushFont(fonts->text_font);
            ImVec2 s = ImGui::CalcTextSize(u.c_str());
            ImGui::PopFont();
            return s;
        }
        ImGui::PushFont(fonts->small_font);
        ImVec2 s = ImGui::CalcTextSize(u.c_str());
        ImGui::PopFont();
        return s;
    };

    const ImVec2 value_sz = ImGui::CalcTextSize(tc.text.c_str());
    const ImVec2 u_sz = unit_size(tc.unit);
    const float row_h = std::max(value_sz.y, u_sz.y);

    if (col_index == 0) {
        RenderOutlinedText(tc.vec, tc.text.c_str());
        if (!tc.unit.empty()) {
            ImGui::SetCursorPos(ImVec2(base.x + value_sz.x, base.y));
            ImGui::SameLine(0.0f, unit_gap);

            if (tc.unit == "%") {
                RenderOutlinedText(unit_col, tc.unit.c_str());
            } else {
                ImGui::PushFont(fonts->small_font);
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
    const float value_left_x = value_right_x - L.value_field_w;

    ImGui::SetCursorPos(ImVec2(value_left_x, base.y));
    right_aligned(tc.vec, L.value_field_w, "%s", tc.text.c_str());

    if (!tc.unit.empty()) {
        ImGui::SetCursorPos(ImVec2(unit_start_x, base.y));
        if (tc.unit == "%") {
            RenderOutlinedText(unit_col, tc.unit.c_str());
        } else {
            ImGui::PushFont(fonts->small_font);
            RenderOutlinedText(unit_col, tc.unit.c_str());
            ImGui::PopFont();
        }
    }

    ImGui::SetCursorPos(ImVec2(base.x, base.y + row_h));
}

void ImGuiCtx::draw_graph_header(const TextCell& tc) {
    ImGui::TableSetColumnIndex(0);
    ImGui::PushFont(fonts->small_font);
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

HudLayout ImGuiCtx::build_layout(const hudTable& table) {
    HudLayout L{};
    L.cols = table.cols;

    unit_gap = 1.0f;

    L.max_unit_w.assign(L.cols, 0.0f);
    L.col_content_w.assign(L.cols, 0.0f);

    ImGui::PushFont(fonts->text_font);
    L.value_field_w = ImGui::CalcTextSize("00000").x;
    ImGui::PopFont();

    float max_col0_w = 0.0f;

    for (const auto& row : table.rows) {
        if (!row.empty() && row[0].has_value()) {
            const Cell& v0 = *row[0];
            if (const auto* tc0 = std::get_if<TextCell>(&v0)) {
                float w = 0.0f;

                ImGui::PushFont(fonts->text_font);
                w += ImGui::CalcTextSize(tc0->text.c_str()).x;
                ImGui::PopFont();

                if (!tc0->unit.empty()) {
                    w += unit_gap;

                    if (tc0->unit == "%") {
                        ImGui::PushFont(fonts->text_font);
                        w += ImGui::CalcTextSize(tc0->unit.c_str()).x;
                        ImGui::PopFont();
                    } else {
                        ImGui::PushFont(fonts->small_font);
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
            if (!tc || tc->unit.empty())
                continue;

            float uw = 0.0f;
            if (tc->unit == "%") {
                ImGui::PushFont(fonts->text_font);
                uw = ImGui::CalcTextSize(tc->unit.c_str()).x;
                ImGui::PopFont();
            } else {
                ImGui::PushFont(fonts->small_font);
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
            L.col_content_w[c] = L.value_field_w + (has_units ? (unit_gap + L.max_unit_w[c]) : 0.0f);
        }
    }

    return L;
}

void ImGuiCtx::draw(clientRes& r) {
    hudTable local_table;
    {
        std::lock_guard lock(r.m);
        local_table = r.table;
    }
    ImGui::SetCurrentContext(imgui);
    ImPlot::SetCurrentContext(implot);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendPlatformName = "headless";
    io.DisplaySize = {float(r.w), float(r.h)};
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = 1.0f / 60.0f;

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowSize({float(r.w), float(r.h)});
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.5f);

    auto& style = ImGui::GetStyle();
    style.CellPadding = ImVec2(5.f, 0.f);
    style.WindowBorderSize = 0.f;
    style.WindowMinSize = ImVec2(4, 4);

    ImVec4 white = ImVec4(1, 1, 1, 1);

    ImGui::PushFont(fonts->text_font);
    ralign_width = ImGui::CalcTextSize("00000").x;
    ImGui::PopFont();

    ImGuiWindowFlags w_flags = ImGuiWindowFlags_NoDecoration;
    ImGui::Begin("HUD", nullptr, w_flags);
    HudLayout layout = build_layout(local_table);

    const int cols = local_table.cols;
    if (cols > 0 && ImGui::BeginTable("overlay", cols, ImGuiTableFlags_NoClip)) {
        ImGui::PushFont(fonts->text_font);

        for (auto& row : local_table.rows) {
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
                    draw_graph_header(*tc);

                    ImGui::TableNextRow();
                    draw_graph_plot(*tc);
                    continue;
                }

                draw_value_with_unit(c, *tc, white, layout);
            }
        }

        ImGui::PopFont();
        ImGui::EndTable();
    }

    uint32_t w = calculate_width(local_table, layout);
    uint32_t h = ImGui::GetCursorPosY() + style.WindowPadding.y +
                 style.ItemSpacing.y + style.CellPadding.y;

    ImGui::End();
    ImGui::Render();

    if (w != r.w || h != r.h) {
        SPDLOG_DEBUG("resizing image from: {} {} to {} {}", r.w, r.h, w, h);
        r.w = w;
        r.h = h;
        r.reinit_dmabuf = true;
        draw(r);
    }
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

VkDescriptorPool ImGuiCtx::create_desc_pool() {
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 128},
    };

    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dp.maxSets = 256;
    dp.poolSizeCount = (uint32_t)(sizeof(sizes)/sizeof(sizes[0]));
    dp.pPoolSizes = sizes;

    vkCreateDescriptorPool(device, &dp, nullptr, &desc_pool);
    return desc_pool;
}

void ImGuiCtx::create_cmd() {
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.queueFamilyIndex = graphicsQueueIdx;
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(device, &cp, nullptr, &cmd_pool);

    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = cmd_pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &ca, &cmd);
}

void ImGuiCtx::teardown() {
    fonts.reset();
    if (!device)
        return;

    vkDeviceWaitIdle(device);
    {
        ImGui::SetCurrentContext(imgui);
        ImPlot::SetCurrentContext(implot);
        ImGui_ImplVulkan_Shutdown();

        if (implot) {
            ImPlot::DestroyContext(implot);
            implot = nullptr;
        }

        if (imgui) {
            ImGui::DestroyContext(imgui);
            imgui = nullptr;
        }

        cmd = VK_NULL_HANDLE;
        fmt = VK_FORMAT_UNDEFINED;
    }

    if (cmd_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, cmd_pool, nullptr);
        cmd_pool = VK_NULL_HANDLE;
    }

    if (desc_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, desc_pool, nullptr);
        desc_pool = VK_NULL_HANDLE;
    }

    device = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    instance = VK_NULL_HANDLE;
    graphicsQueue = VK_NULL_HANDLE;
    graphicsQueueIdx = 0;
}
