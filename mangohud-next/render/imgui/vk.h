#include "imgui.h"
#include "implot.h"
#include <vulkan/vulkan.h>
#include "backends/imgui_impl_vulkan.h"
#include "../vulkan_ctx.h"
#include "font/font.h"

class ImGuiVK {
public:
    ImGuiContext* imgui;
    ImPlotContext* implot;
    std::shared_ptr<Font> fonts;

    ImGuiVK(std::shared_ptr<VkCtx> vk_, ImGuiCtx* imgui_ctx_) : vk(std::move(vk_)), imgui_ctx(imgui_ctx_) {
        std::scoped_lock mutex(vk->m, imgui_ctx->m);
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

        ImGui_ImplVulkan_InitInfo ii{};
        ii.Instance = vk->instance;
        ii.PhysicalDevice = vk->physicalDevice;
        ii.Device = vk->device;
        ii.QueueFamily = vk->graphicsQueueFamilyIndex;
        ii.Queue = vk->graphicsQueue;
        ii.DescriptorPool = create_desc_pool();

        ii.MinImageCount = 256;
        ii.ImageCount = 256;
        ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        ii.UseDynamicRendering = true;
        ii.PipelineRenderingCreateInfo = VkPipelineRenderingCreateInfoKHR{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
        ii.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        ii.PipelineRenderingCreateInfo.pColorAttachmentFormats = &vk->fmt;

        ImGui_ImplVulkan_Init(&ii);
        fonts = std::make_shared<Font>([] {
            ImGui_ImplVulkan_CreateFontsTexture();
        });
    };

    std::mutex& mutex() {
        return vk->m;
    }

    void record_cmd(slot_t& buf, uint32_t w, uint32_t h) {
        vkResetFences(vk->device, 1, &buf.sync.fence);
        vkResetCommandBuffer(buf.sync.cmd, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(buf.sync.cmd, &bi);

        vk->transition_image(buf.sync.cmd, buf.source.image_res.image, buf.source.image_res.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        buf.source.image_res.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkClearValue clear{};
        VkRenderingAttachmentInfo colorAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        assert(buf.source.image_res.view != VK_NULL_HANDLE);
        colorAtt.imageView = buf.source.image_res.view;
        colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        clear.color.float32[0] = 0.0f;
        clear.color.float32[1] = 0.0f;
        clear.color.float32[2] = 0.0f;
        clear.color.float32[3] = 0.0f;
        colorAtt.clearValue = clear;

        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea.offset = {0, 0};
        ri.renderArea.extent = {w, h};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &colorAtt;

        vkCmdBeginRendering(buf.sync.cmd, &ri);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), buf.sync.cmd);
        vkCmdEndRendering(buf.sync.cmd);
    }

    ~ImGuiVK() {
        if (!vk->device)
            return;

        std::scoped_lock lock(vk->m, imgui_ctx->m);
        vkDeviceWaitIdle(vk->device);
        ImGui::SetCurrentContext(imgui);
        ImPlot::SetCurrentContext(implot);
        ImGui_ImplVulkan_Shutdown();
        if (desc_pool) {
            vkDestroyDescriptorPool(vk->device, desc_pool, nullptr);
            desc_pool = VK_NULL_HANDLE;
        }
        ImPlot::DestroyContext(implot);
        ImGui::DestroyContext(imgui);
        implot = nullptr;
        imgui = nullptr;
    };

private:
    std::shared_ptr<VkCtx> vk;
    ImGuiCtx* imgui_ctx;
    VkDescriptorPool desc_pool = VK_NULL_HANDLE;

    VkDescriptorPool create_desc_pool() {
        VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 },
        };

        VkDescriptorPoolCreateInfo dp{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        dp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        dp.maxSets = 256;
        dp.poolSizeCount = 1;
        dp.pPoolSizes = sizes;

        VkResult r = vkCreateDescriptorPool(vk->device, &dp, nullptr, &desc_pool);
        if (r != VK_SUCCESS) desc_pool = VK_NULL_HANDLE;
        return desc_pool;
    }

};
