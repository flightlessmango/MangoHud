#include <deque>
#include "vulkan_ctx.h"
#include "imgui_ctx.h"

#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_vulkan.h"
#include "font/font.h"
#include "../server/config.h"

ImGuiCtx::ImGuiCtx(VulkanContext& vk) : vk(vk) {
    const VkFormat fmt = VK_FORMAT_B8G8R8A8_SRGB;
    create_cmd(vk.device(), vk.graphicsQueueFamilyIndex());
    desc_pool = create_desc_pool(vk.device());
    init(desc_pool, fmt);
    ImGui_ImplVulkan_CreateFontsTexture();
};

bool ImGuiCtx::init(VkDescriptorPool pool, VkFormat colorFmt) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::StyleColorsDark();
    fonts = std::make_unique<Font>();
    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance = vk.instance();
    ii.PhysicalDevice = vk.physicalDevice();
    ii.Device = vk.device();
    ii.QueueFamily = vk.graphicsQueueFamilyIndex();
    ii.Queue = vk.graphicsQueue();
    ii.DescriptorPool = pool;

    ii.MinImageCount = 2;
    ii.ImageCount = 2;
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ii.UseDynamicRendering = true;
    ii.PipelineRenderingCreateInfo = VkPipelineRenderingCreateInfoKHR{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    ii.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    ii.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFmt;
    return ImGui_ImplVulkan_Init(&ii);
};

void ImGuiCtx::add_to_queue(frame& frame_) {
    std::lock_guard<std::mutex> lock(queue_m);
    frame_queue.push_back(frame_);
};

void ImGuiCtx::drain_queue() {
    std::lock_guard<std::mutex> lock(queue_m);
    std::deque<frame> queue{};
    frame_queue.swap(queue);

    for (auto& frame_ : queue)
        render(frame_);
};

void ImGuiCtx::right_aligned(ImVec4& col, const char *fmt, ...)
{
   ImVec2 pos = ImGui::GetCursorPos();
   char buffer[32] {};

   va_list args;
   va_start(args, fmt);
   vsnprintf(buffer, sizeof(buffer), fmt, args);
   va_end(args);

   float off_x = ImGui::CalcTextSize("A").x * 4;

   ImVec2 sz = ImGui::CalcTextSize(buffer);
   ImGui::SetCursorPosX(pos.x + off_x - sz.x);
//    ImGui::TextColored(col,"%s", buffer);
   RenderOutlinedText(col, buffer);
}

float ImGuiCtx::calculate_width(frame& frame_) {
    const int cols = frame_.table.cols;
    auto& style = ImGui::GetStyle();

    // Match your actual layout choices
    const float unit_gap = 1.0f; // must match SameLine(0, unit_gap) when drawing units

    std::vector<float> col_w(cols, 0.0f);

    for (auto& row : frame_.table.rows) {
        for (int c = 0; c < cols; c++) {
            if (c >= (int)row.size()) continue;

            const auto& opt = row[c];
            if (!opt) continue;

            const Cell& v = *opt;
            const auto* tc = std::get_if<TextCell>(&v);
            if (!tc) continue;

            // measure "value + optional unit" block width using the fonts you render with
            float w = 0.0f;

            if (!tc->text.empty()) {
                ImGui::PushFont(fonts->text_font);
                w += ImGui::CalcTextSize(tc->text.c_str()).x;
                if (!tc->unit.empty() && tc->unit == "%")
                    w += ImGui::CalcTextSize(tc->unit.c_str()).x;

                ImGui::PopFont();
            }

            if (!tc->unit.empty()) {
                ImGui::PushFont(fonts->small_font);
                w += unit_gap;
                w += ImGui::CalcTextSize(tc->unit.c_str()).x;
                ImGui::PopFont();
            }

            col_w[c] = std::max(col_w[c], w);
        }
    }

    float min_w = style.WindowPadding.x * 2.0f;
    for (float w : col_w) min_w += w;

    // Keep your empirically-correct terms
    min_w += style.ItemSpacing.x * (cols + 1);
    min_w += style.FramePadding.x * cols;
    min_w += style.WindowPadding.x * 2;

    return min_w;
}


float ImGuiCtx::calculate_height(frame& frame_) {
    auto& style = ImGui::GetStyle();

    ImGui::PushFont(fonts->text_font);

    const float line_h = ImGui::GetTextLineHeight();
    const int rows = (int)frame_.table.rows.size();

    ImGui::PopFont();

    if (rows == 0)
        return style.WindowPadding.y * 2.0f;

    int graph_count = 0;
    for (auto& row : frame_.table.rows) {
        for (auto& cell : row) {
            if (!cell.has_value())
                continue;

            Cell& c = *cell;
            if (!std::holds_alternative<TextCell>(c))
                continue;

            auto& tc = std::get<TextCell>(c);
            if (!tc.data.empty())
                graph_count++;
        }
    }


    float h = 0.0f;
    h += line_h * rows;
    h += style.ItemSpacing.y * (rows - 1);
    h += graph_count * 25; // we currently default to 50, but real height seems to be size / 2

    return h;
}

static inline double TransformForward_Custom(double v, void*) {
    if (v > 50)
        v = 49.9;

    return v;
}

static inline double TransformInverse_Custom(double v, void*) {
   return v;
}

void ImGuiCtx::render(frame& frame_) {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "headless";
    static ImVec2 window_size {500,500};
    io.DisplaySize = window_size;
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(window_size);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(5, 0));
    ImGui::SetNextWindowBgAlpha(0.5);

    ImGuiWindowFlags w_flags = ImGuiWindowFlags_NoDecoration;
    ImGui::Begin("mangohud", nullptr, w_flags);

    int cols = frame_.table.cols;
    float row_right = 0.0f;
    float hud_width = 0;
    static float max_width = 0;
    float hud_height = 0;
    if (ImGui::BeginTable("overlay", cols, ImGuiTableFlags_NoClip)) {
        // ImGui::TableSetupColumn("value2", ImGuiTableColumnFlags_WidthFixed);
        ImGui::PushFont(fonts->text_font);
        for (auto& row : frame_.table.rows) {

            ImGui::TableNextRow();
            for (auto& cell : row) {
                if (!cell.has_value())
                    continue;

                Cell& c = *cell;
                if (!std::holds_alternative<TextCell>(c))
                    continue;

                auto& tc = std::get<TextCell>(c);

                ImGui::TableNextColumn();
                if (!tc.data.empty()) {

                    float width = (ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x) - ImGui::GetStyle().WindowPadding.x;
                    if (ImGui::BeginChild("my_child_window", ImVec2(width, 50), false, ImGuiWindowFlags_NoDecoration)) {
                        if (ImPlot::BeginPlot("My Plot", ImVec2(width, 50), ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs)) {
                            ImPlotStyle& style = ImPlot::GetStyle();
                            style.Colors[ImPlotCol_PlotBg]      = ImVec4(0.92f, 0.92f, 0.95f, 0.00f);
                            style.Colors[ImPlotCol_AxisGrid]    = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
                            style.Colors[ImPlotCol_AxisTick]    = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
                            style.Colors[ImPlotCol_FrameBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
                            style.PlotPadding.x = 0;
                            ImPlotAxisFlags ax_flags_x = ImPlotAxisFlags_NoDecorations;
                            ImPlotAxisFlags ax_flags_y = ImPlotAxisFlags_NoDecorations;
                            ImPlot::SetupAxes(nullptr, nullptr, ax_flags_x, ax_flags_y);
                            ImPlot::SetupAxisScale(ImAxis_Y1, TransformForward_Custom, TransformInverse_Custom);
                            ImPlot::SetupAxesLimits(0, 200, 0, 50, ImGuiCond_Always);
                            ImPlot::SetNextLineStyle(ImVec4(0,1,0,1), 1.5);
                            ImPlot::PlotLine("frametime line", tc.data.data(), tc.data.size());
                        }
                        ImPlot::EndPlot();
                    }
                    ImGui::EndChild();
                }
                if (&cell == &row.front())
                    RenderOutlinedText(tc.vec, tc.text.c_str());
                else
                    right_aligned(tc.vec, "%s", tc.text.c_str());

                if (!tc.unit.empty()) {
                    ImGui::SameLine(0, 1);
                    ImVec4 white = ImVec4(1,1,1,1);
                    if (tc.unit == "%") {
                        RenderOutlinedText(white, tc.unit.c_str());
                        continue;
                    }
                    ImGui::PushFont(fonts->small_font);
                    RenderOutlinedText(white, tc.unit.c_str());
                    ImGui::PopFont();
                }
            }
        }
        window_size = {calculate_width(frame_), calculate_height(frame_)};
        ImGui::PopFont();
        ImGui::EndTable();
        ImGui::PopStyleVar();
        ImGui::End();
        ImGui::Render();

        submit_draw(frame_);
    }
}

#include "imgui.h"

void ImGuiCtx::RenderOutlinedText(ImVec4 textColor, const char* text)
{
    if (!text || !text[0]) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    float fontSize = ImGui::GetFontSize();

    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImU32 tc = ImGui::ColorConvertFloat4ToU32(textColor);

    float t = 1.5;
    ImU32 oc = ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 1));

    dl->AddText(font, fontSize, ImVec2(pos.x - t, pos.y),     oc, text);
    dl->AddText(font, fontSize, ImVec2(pos.x + t, pos.y),     oc, text);
    dl->AddText(font, fontSize, ImVec2(pos.x,     pos.y - t), oc, text);
    dl->AddText(font, fontSize, ImVec2(pos.x,     pos.y + t), oc, text);

    dl->AddText(font, fontSize, pos, tc, text);

    ImVec2 sz = ImGui::CalcTextSize(text);
    ImGui::Dummy(sz);
}

VkDescriptorPool ImGuiCtx::create_desc_pool(VkDevice dev) {
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

    VkDescriptorPool pool = VK_NULL_HANDLE;
    vkCreateDescriptorPool(dev, &dp, nullptr, &pool);
    return pool;
}

void ImGuiCtx::create_cmd(VkDevice dev, uint32_t qfam) {
    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.queueFamilyIndex = qfam;
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(dev, &cp, nullptr, &cmd_pool);

    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = cmd_pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    vkAllocateCommandBuffers(dev, &ca, &cmd);

    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(dev, &fi, nullptr, &fence);
}

void ImGuiCtx::submit_draw(frame& frame_) {
    VkDevice dev = vk.device();

    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(dev, 1, &fence);
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkImageLayout oldL = frame_.layout;
    VkImageLayout newL = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    transition_image(cmd, frame_.target.image, oldL, newL);
    frame_.layout = newL;
    VkClearValue clear{};
    clear.color.float32[0] = 0.f;
    clear.color.float32[1] = 0.f;
    clear.color.float32[2] = 0.f;
    clear.color.float32[3] = 0.f;

    VkRenderingAttachmentInfo colorAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAtt.imageView = frame_.target.view;
    colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.clearValue = clear;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.offset = {0, 0};
    ri.renderArea.extent = {frame_.w, frame_.h};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &colorAtt;

    vkCmdBeginRendering(cmd, &ri);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
    transition_image(cmd, frame_.target.image,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    frame_.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {frame_.w, frame_.h, 1};

    vkCmdCopyImage(cmd,
    frame_.target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    frame_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    1, &region);

    transition_image(cmd, frame_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(vk.graphicsQueue(), 1, &si, fence);
    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
}

void ImGuiCtx::transition_image(VkCommandBuffer cmd,
                            VkImage image,
                            VkImageLayout oldLayout,
                            VkImageLayout newLayout)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = 0;
    VkPipelineStageFlags dstStage = 0;

    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    vkCmdPipelineBarrier(cmd,
                        srcStage, dstStage,
                        0,
                        0, nullptr,
                        0, nullptr,
                        1, &barrier);
}

void ImGuiCtx::teardown() {
    VkDevice dev = vk.device();
    if (dev == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(dev);

    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    ImPlot::CreateContext();

    if (fence) {
        vkDestroyFence(dev, fence, nullptr);
        fence = VK_NULL_HANDLE;
    }

    if (cmd != VK_NULL_HANDLE && cmd_pool != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(dev, cmd_pool, 1, &cmd);
        cmd = VK_NULL_HANDLE;
    }

    if (cmd_pool) {
        vkDestroyCommandPool(dev, cmd_pool, nullptr);
        cmd_pool = VK_NULL_HANDLE;
    }

    if (desc_pool) {
        vkDestroyDescriptorPool(dev, desc_pool, nullptr);
        desc_pool = VK_NULL_HANDLE;
    }
}
