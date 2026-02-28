#include "vulkan.h"

struct overlay_resources {
    std::shared_ptr<const vkroots::VkDeviceDispatch> d = nullptr;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkDescriptorPool dp = VK_NULL_HANDLE;

    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule fs = VK_NULL_HANDLE;

    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmd;
    std::vector<VkFence> cmd_fences;

    std::vector<VkSemaphore> overlay_done;

    std::mutex m;
    overlay_resources(std::shared_ptr<const vkroots::VkDeviceDispatch> d_) : d(d_) {}

    ~overlay_resources() {
        if (!d) return;

        d->DeviceWaitIdle(d->Device);

        if (dp) {
            d->DestroyDescriptorPool(d->Device, dp, nullptr);
            dp = VK_NULL_HANDLE;
        }

        if (pl) {
            d->DestroyPipelineLayout(d->Device, pl, nullptr);
            pl = VK_NULL_HANDLE;
        }

        if (dsl) {
            d->DestroyDescriptorSetLayout(d->Device, dsl, nullptr);
            dsl = VK_NULL_HANDLE;
        }

        if (sampler) {
            d->DestroySampler(d->Device, sampler, nullptr);
            sampler = VK_NULL_HANDLE;
        }

        if (vs) {
            d->DestroyShaderModule(d->Device, vs, nullptr);
            vs = VK_NULL_HANDLE;
        }

        if (fs) {
            d->DestroyShaderModule(d->Device, fs, nullptr);
            fs = VK_NULL_HANDLE;
        }

        if (!cmd_fences.empty()) {
            for (auto& fence : cmd_fences) {
                if (fence)
                    d->DestroyFence(d->Device, fence, nullptr);

                fence = VK_NULL_HANDLE;
            }

        }

        if (cmd_pool && !cmd.empty()) {
            d->FreeCommandBuffers(d->Device, cmd_pool,
                                         (uint32_t)cmd.size(), cmd.data());
            for (auto& c : cmd)
                c = VK_NULL_HANDLE;
        }

        if (cmd_pool) {
            d->DestroyCommandPool(d->Device, cmd_pool, nullptr);
            cmd_pool = VK_NULL_HANDLE;
        }

        for (auto& sema : overlay_done) {
            d->DestroySemaphore(d->Device, sema, nullptr);
            sema = VK_NULL_HANDLE;
        }
    }
};

struct swapchain_data {
    std::shared_ptr<const vkroots::VkDeviceDispatch> d;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkColorSpaceKHR colorspace;

    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> fb;

    VkRenderPass rp = VK_NULL_HANDLE;
    VkPipeline pipe = VK_NULL_HANDLE;

    std::mutex m;
    swapchain_data(std::shared_ptr<const vkroots::VkDeviceDispatch> d_) : d(d_) {}
    ~swapchain_data() {
        if (!d)
            return;

        std::lock_guard lock(m);

        d->DeviceWaitIdle(d->Device);

        for (auto& framebuf : fb) {
            if (framebuf)
                d->DestroyFramebuffer(d->Device, framebuf, nullptr);

            framebuf = VK_NULL_HANDLE;
        }

        for (auto& view : views) {
            if (view)
                d->DestroyImageView(d->Device, view, nullptr);

            view = VK_NULL_HANDLE;
        }

        if (rp) {
            d->DestroyRenderPass(d->Device, rp, nullptr);
            rp = VK_NULL_HANDLE;
        }

        if (pipe) {
            d->DestroyPipeline(d->Device, pipe, nullptr);
            pipe = VK_NULL_HANDLE;
        }
    }
};

class Layer {
public:
    std::shared_ptr<OverlayVK> overlay_vk;
    std::shared_ptr<IPCClient> ipc;

    std::shared_ptr<const vkroots::VkDeviceDispatch> d;
    PFN_vkSetDeviceLoaderData loader_data = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT g_vkSetDebugUtilsObjectNameEXT = nullptr;
    std::mutex swapchain_mtx;
    std::unordered_map<VkSwapchainKHR, std::shared_ptr<swapchain_data>> swapchains;
    std::shared_ptr<overlay_resources> ovl_res;
    std::unordered_map<VkQueue, uint32_t> queue_family;
    std::mutex q_family_mtx;

    Layer() {
        ipc = std::make_shared<IPCClient>(this);
        overlay_vk = std::make_shared<OverlayVK>(this);
    }

    void SetName(VkDevice device, VkObjectType type, uint64_t handle, const char* fmt, ...) {
        if (!g_vkSetDebugUtilsObjectNameEXT || handle == 0) {
            return;
        }

        char name[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(name, sizeof(name), fmt, args);
        va_end(args);

        VkDebugUtilsObjectNameInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = type;
        info.objectHandle = handle;
        info.pObjectName = name;

        g_vkSetDebugUtilsObjectNameEXT(device, &info);
    }

    std::shared_ptr<swapchain_data> get_swapchain_data(VkSwapchainKHR swap) {
        std::lock_guard lock(swapchain_mtx);
        auto it = swapchains.find(swap);
        if (it != swapchains.end())
                return it->second;

        SPDLOG_ERROR("No swapchain available, critical error");
        std::abort();
    }

    void create_swapchain_data(VkSwapchainKHR* pSwapchain, const VkSwapchainCreateInfoKHR* pCreateInfo, const vkroots::VkDeviceDispatch* pDispatch) {
        auto d = std::make_shared<const vkroots::VkDeviceDispatch>(*pDispatch);
        auto sc = std::make_shared<swapchain_data>(d);

        sc->format = pCreateInfo->imageFormat;
        sc->extent = pCreateInfo->imageExtent;
        sc->colorspace = pCreateInfo->imageColorSpace;

        uint32_t count = 0;
        VkResult r = pDispatch->GetSwapchainImagesKHR(pDispatch->Device, *pSwapchain, &count, nullptr);
        if (r != VK_SUCCESS || count == 0) {
            pDispatch->DestroySwapchainKHR(pDispatch->Device, *pSwapchain, nullptr);
            SPDLOG_ERROR("GetSwapchainImagesKHR {}", string_VkResult(r));
        }

        sc->images.resize(count);
        r = pDispatch->GetSwapchainImagesKHR(pDispatch->Device, *pSwapchain, &count, sc->images.data());
        if (r != VK_SUCCESS) {
            pDispatch->DestroySwapchainKHR(pDispatch->Device, *pSwapchain, nullptr);
            SPDLOG_ERROR("GetSwapchainImagesKHR {}", string_VkResult(r));
        }

        sc->views.resize(count, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < count; i++) {
            VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vi.image = sc->images[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = sc->format;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.baseMipLevel = 0;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.baseArrayLayer = 0;
            vi.subresourceRange.layerCount = 1;

            r = pDispatch->CreateImageView(pDispatch->Device, &vi, nullptr, &sc->views[i]);
            if (r != VK_SUCCESS) {
                for (uint32_t j = 0; j < i; j++)
                    if (sc->views[j]) pDispatch->DestroyImageView(pDispatch->Device, sc->views[j], nullptr);
                pDispatch->DestroySwapchainKHR(pDispatch->Device, *pSwapchain, nullptr);
                SPDLOG_ERROR("CreateImageView {}", string_VkResult(r));
            }
        }

        VkAttachmentDescription ad{};
        ad.format = sc->format;
        ad.samples = VK_SAMPLE_COUNT_1_BIT;
        ad.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        ad.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ad.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        ad.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        ad.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        ad.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference aref{};
        aref.attachment = 0;
        aref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sp{};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1;
        sp.pColorAttachments = &aref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        rpci.attachmentCount = 1;
        rpci.pAttachments = &ad;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sp;
        rpci.dependencyCount = 1;
        rpci.pDependencies = &dep;

        r = pDispatch->CreateRenderPass(pDispatch->Device, &rpci, nullptr, &sc->rp);
        if (r != VK_SUCCESS) {
            for (auto v : sc->views) if (v) pDispatch->DestroyImageView(pDispatch->Device, v, nullptr);
            pDispatch->DestroySwapchainKHR(pDispatch->Device, *pSwapchain, nullptr);
            SPDLOG_ERROR("CreateImageView {}", string_VkResult(r));
        }

        sc->fb.resize(count, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < count; i++) {
            VkImageView att = sc->views[i];

            VkFramebufferCreateInfo fci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            fci.renderPass = sc->rp;
            fci.attachmentCount = 1;
            fci.pAttachments = &att;
            fci.width = sc->extent.width;
            fci.height = sc->extent.height;
            fci.layers = 1;

            r = pDispatch->CreateFramebuffer(pDispatch->Device, &fci, nullptr, &sc->fb[i]);
            if (r != VK_SUCCESS) {
                for (uint32_t j = 0; j < i; j++)
                    if (sc->fb[j]) pDispatch->DestroyFramebuffer(pDispatch->Device, sc->fb[j], nullptr);
                if (sc->rp) pDispatch->DestroyRenderPass(pDispatch->Device, sc->rp, nullptr);
                for (auto v : sc->views) if (v) pDispatch->DestroyImageView(pDispatch->Device, v, nullptr);
                pDispatch->DestroySwapchainKHR(pDispatch->Device, *pSwapchain, nullptr);
                SPDLOG_ERROR("CreateFramebuffer {}", string_VkResult(r));
            }
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = ovl_res->vs;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = ovl_res->fs;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vis{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

        VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{};
        vp.x = 0.0f;
        vp.y = 0.0f;
        vp.width = (float)sc->extent.width;
        vp.height = (float)sc->extent.height;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = sc->extent;

        VkPipelineViewportStateCreateInfo vpstate{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        vpstate.viewportCount = 1;
        vpstate.pViewports = &vp;
        vpstate.scissorCount = 1;
        vpstate.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cbatt{};
        cbatt.blendEnable = VK_TRUE;

        cbatt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cbatt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cbatt.colorBlendOp = VK_BLEND_OP_ADD;

        cbatt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cbatt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cbatt.alphaBlendOp = VK_BLEND_OP_ADD;

        cbatt.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        cb.attachmentCount = 1;
        cb.pAttachments = &cbatt;

        VkGraphicsPipelineCreateInfo gp{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vis;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vpstate;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pColorBlendState = &cb;
        gp.layout = ovl_res->pl;
        gp.renderPass = sc->rp;
        gp.subpass = 0;

        r = d->CreateGraphicsPipelines(d->Device, VK_NULL_HANDLE, 1, &gp, nullptr, &sc->pipe);
        if (r == VK_SUCCESS)
            SetName(d->Device, VK_OBJECT_TYPE_PIPELINE, uint64_t(sc->pipe), "mangohud_pipeline");
        else
            SPDLOG_ERROR("CreateGraphicsPipelines {}", string_VkResult(r));

        {
            std::lock_guard lock(swapchain_mtx);
            swapchains[*pSwapchain] = sc;
        }
    }

    void init_overlay_resources(const VkSwapchainCreateInfoKHR* pCreateInfo, const vkroots::VkDeviceDispatch* pDispatch, uint32_t image_count) {
        if (ovl_res)
            return;

        auto d = std::make_shared<const vkroots::VkDeviceDispatch>(*pDispatch);
        ovl_res = std::make_shared<overlay_resources>(d);

        if (!ovl_res->vs) {
            ovl_res->vs = make_shader(d.get(), d->Device, overlay_vert_spv, sizeof(overlay_vert_spv));
            SetName(d->Device, VK_OBJECT_TYPE_SHADER_MODULE, uint64_t(ovl_res->vs), "mangohud_vert_shader");
        }

        if (!ovl_res->fs) {
            ovl_res->fs = make_shader(d.get(), d->Device, overlay_frag_spv, sizeof(overlay_frag_spv));
            SetName(d->Device, VK_OBJECT_TYPE_SHADER_MODULE, uint64_t(ovl_res->fs), "mangohud_frag_shader");
        }

        if (!ovl_res->sampler) {
            VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            sci.magFilter = VK_FILTER_NEAREST;
            sci.minFilter = VK_FILTER_NEAREST;
            sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sci.minLod = 0.0f;
            sci.maxLod = 0.0f;
            sci.maxAnisotropy = 1.0f;
            sci.unnormalizedCoordinates = VK_FALSE;

            VkResult r = d->CreateSampler(d->Device, &sci, nullptr, &ovl_res->sampler);
            if (r != VK_SUCCESS)
                SPDLOG_ERROR("CreateSampler {}", string_VkResult(r));
            SetName(d->Device, VK_OBJECT_TYPE_SAMPLER, uint64_t(ovl_res->sampler), "mangohud_sampler");
        }

        if (!ovl_res->dsl) {
            VkDescriptorSetLayoutBinding b{};
            b.binding = 0;
            b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b.descriptorCount = 1;
            b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo dsci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            dsci.bindingCount = 1;
            dsci.pBindings = &b;

            VkResult r = d->CreateDescriptorSetLayout(d->Device, &dsci, nullptr, &ovl_res->dsl);
            if (r != VK_SUCCESS)
                SPDLOG_ERROR("CreateDescriptorSetLayout {}", string_VkResult(r));
            SetName(d->Device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, uint64_t(ovl_res->dsl), "mangohud_descriptor_set_layout");
        }

        if (!ovl_res->dp) {
            VkDescriptorPoolSize ps{};
            ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            ps.descriptorCount = image_count * 2; // * 2 because we need one per cache as well

            VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            dpci.maxSets = image_count * 2;
            dpci.poolSizeCount = 1;
            dpci.pPoolSizes = &ps;

            VkResult r = d->CreateDescriptorPool(d->Device, &dpci, nullptr, &ovl_res->dp);
            if (r != VK_SUCCESS)
                SPDLOG_ERROR("CreateDescriptorPool {}", string_VkResult(r));
            SetName(d->Device, VK_OBJECT_TYPE_DESCRIPTOR_POOL, uint64_t(ovl_res->dp), "mangohud_descriptor_pool");
        }

        if (!ovl_res->pl) {
            VkPushConstantRange pcr{};
            pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pcr.offset = 0;
            pcr.size = sizeof(OverlayPushConsts);

            VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            plci.setLayoutCount = 1;
            plci.pSetLayouts = &ovl_res->dsl;
            plci.pushConstantRangeCount = 1;
            plci.pPushConstantRanges = &pcr;

            VkResult r = d->CreatePipelineLayout(d->Device, &plci, nullptr, &ovl_res->pl);
            if (r != VK_SUCCESS)
                SPDLOG_ERROR("CreatePipelineLayout {}", string_VkResult(r));
            SetName(d->Device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, uint64_t(ovl_res->pl), "mangohud_pipeline_layout");
        }

        ovl_res->cmd_fences.resize(image_count);
        for (size_t i = 0; i < image_count; i++) {
            if (ovl_res->cmd_fences[i] == VK_NULL_HANDLE) {
                VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
                fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

                VkResult r = d->CreateFence(d->Device, &fci, nullptr, &ovl_res->cmd_fences[i]);
                if (r != VK_SUCCESS) {
                    d->DestroyFence(d->Device, ovl_res->cmd_fences[i], nullptr);
                    ovl_res->cmd_fences[i] = VK_NULL_HANDLE;
                    SPDLOG_ERROR("CreateFence {}", string_VkResult(r));
                }
                SetName(d->Device, VK_OBJECT_TYPE_FENCE, (uint64_t)ovl_res->cmd_fences[i],
                        "mangohud_overlay_cmd_fence_%zu", i);
            }
        }

        ovl_res->overlay_done.resize(image_count);
        for (size_t i = 0; i < image_count; ++i) {
            auto& sema = ovl_res->overlay_done[i];
            if (sema == VK_NULL_HANDLE) {
                VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
                VkResult r = d->CreateSemaphore(d->Device, &si, nullptr, &sema);
                if (r != VK_SUCCESS)
                    SPDLOG_ERROR("CreateSemaphore {}", string_VkResult(r));

                SetName(d->Device, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)sema,
                        "mangohud_overlay_done_semaphore_%zu", i);
            }
        }
    }

    void init_cmd(VkQueue queue) {
        auto d = ovl_res->d;
        uint32_t image_count = ovl_res->cmd_fences.size();
        if (ovl_res->cmd_pool == VK_NULL_HANDLE) {
            VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
            pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            {
                std::lock_guard lock(q_family_mtx);
                auto it = queue_family.find(queue);
                if (it != queue_family.end())
                    pci.queueFamilyIndex = it->second;
            }
            VkResult r = d->CreateCommandPool(d->Device, &pci, nullptr, &ovl_res->cmd_pool);
            if (r != VK_SUCCESS)
                SPDLOG_ERROR("CreateCommandPool {}", string_VkResult(r));
        }

        if (ovl_res->cmd.size() != image_count) {
            VkResult r = d->ResetCommandPool(d->Device, ovl_res->cmd_pool, 0);
            if (r != VK_SUCCESS)
                SPDLOG_ERROR("ResetCommandPool {}", string_VkResult(r));

            ovl_res->cmd.assign(image_count, VK_NULL_HANDLE);

            VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            ai.commandPool = ovl_res->cmd_pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = image_count;

            r = d->AllocateCommandBuffers(d->Device, &ai, ovl_res->cmd.data());
            if (r != VK_SUCCESS)
                SPDLOG_ERROR("AllocateCommandBuffers {}", string_VkResult(r));

            for (VkCommandBuffer cb : ovl_res->cmd)
                loader_data(d->Device, cb);
        }
    }

    ~Layer() {
        swapchains.clear();
    }

private:
    VkShaderModule make_shader(const vkroots::VkDeviceDispatch* d, VkDevice dev,
                                        const uint32_t* code, size_t codeSizeBytes)
    {
        VkShaderModuleCreateInfo sci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        sci.codeSize = codeSizeBytes;
        sci.pCode = code;

        VkShaderModule m = VK_NULL_HANDLE;
        if (d->CreateShaderModule(dev, &sci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
        return m;
    }

};

