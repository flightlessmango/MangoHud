#include "vulkan.h"

bool OverlayVK::draw(const vkroots::VkDeviceDispatch* d, VkSwapchainKHR swapchain,
            uint32_t family, uint32_t image_count, uint32_t img_idx,
            VkPresentInfoKHR* pPresentInfo, VkQueue queue)
{

    dispatch = d;
    if (ipc->fdinfo.gbm_fd < 0)
        return false;

    std::shared_ptr<swapchain_data> sc = get_swapchain_data(swapchain);
    if (!sc)
        return false;

    if (ext && release_fence) {
        VkResult st = d->GetFenceStatus(d->Device, release_fence);
        if (st == VK_SUCCESS) {
            d->DestroyFence(d->Device, release_fence, nullptr);
            release_fence = VK_NULL_HANDLE;
            d->DestroySemaphore(d->Device, release_sema, nullptr);
            release_sema = VK_NULL_HANDLE;
        } else if (st != VK_NOT_READY) {
            return false;
        }
    }

    bool can_refresh = false;
    if (ext && !release_fence) {
        acquire_fd = ipc->ready_frame();
        can_refresh = (acquire_fd >= 0);
        if (can_refresh)
            close(acquire_fd);
    }

    if (ipc->needs_import.load() && can_refresh) {
        d->DeviceWaitIdle(d->Device);
        sc->ovl.reset();
        ext.reset();
        ipc->needs_import.store(false);
    }

    if (!ext)
        if (import_dmabuf(d) != VK_SUCCESS)
            return false;

    if (cmd_resources(d, sc.get(), family, image_count) != VK_SUCCESS)
        return false;

    if (pipeline(d, sc.get()) != VK_SUCCESS)
            return false;

    if (sample_dmabuf(d, sc.get(), img_idx, ext->width, ext->height, can_refresh) != VK_SUCCESS)
        return false;

    if (semaphores(d, sc.get(), img_idx, pPresentInfo, queue, can_refresh) != VK_SUCCESS)
        return false;

    return true;
}

VkResult OverlayVK::pipeline(const vkroots::VkDeviceDispatch* d, swapchain_data* sc)
{
    if (!sc->ovl) sc->ovl = std::make_unique<overlay_pipeline>();
    sc->ovl->d = d;
    if (sc->ovl->pipe && sc->ovl->pl && sc->ovl->ds && sc->ovl->sampler)
        return VK_SUCCESS;

    if (!sc->ovl->vs) {
        sc->ovl->vs = make_shader(d, d->Device, overlay_vert_spv, sizeof(overlay_vert_spv));
        if (!sc->ovl->vs) return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!sc->ovl->fs) {
        sc->ovl->fs = make_shader(d, d->Device, overlay_frag_spv, sizeof(overlay_frag_spv));
        if (!sc->ovl->fs) return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!sc->ovl->sampler) {
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

        VkResult r = d->CreateSampler(d->Device, &sci, nullptr, &sc->ovl->sampler);
        if (r != VK_SUCCESS) return r;
        SetName(d->Device, VK_OBJECT_TYPE_SAMPLER, uint64_t(sc->ovl->sampler), "mangohud_sampler");
    }

    if (!sc->ovl->dsl) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dsci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        dsci.bindingCount = 1;
        dsci.pBindings = &b;

        VkResult r = d->CreateDescriptorSetLayout(d->Device, &dsci, nullptr, &sc->ovl->dsl);
        if (r != VK_SUCCESS) return r;
        SetName(d->Device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, uint64_t(sc->ovl->dsl), "mangohud_descriptor_set_layout");
    }

    if (!sc->ovl->dp) {
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = 1;

        VkDescriptorPoolCreateInfo dpci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;

        VkResult r = d->CreateDescriptorPool(d->Device, &dpci, nullptr, &sc->ovl->dp);
        if (r != VK_SUCCESS) return r;
        SetName(d->Device, VK_OBJECT_TYPE_DESCRIPTOR_POOL, uint64_t(sc->ovl->dp), "mangohud_descriptor_pool");

        VkDescriptorSetAllocateInfo as{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        as.descriptorPool = sc->ovl->dp;
        as.descriptorSetCount = 1;
        as.pSetLayouts = &sc->ovl->dsl;

        r = d->AllocateDescriptorSets(d->Device, &as, &sc->ovl->ds);
        if (r != VK_SUCCESS) return r;
    }

    if (!sc->ovl->pl) {
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(OverlayPushConsts);

        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &sc->ovl->dsl;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcr;

        VkResult r = d->CreatePipelineLayout(d->Device, &plci, nullptr, &sc->ovl->pl);
        if (r != VK_SUCCESS) return r;
        SetName(d->Device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, uint64_t(sc->ovl->pl), "mangohud_pipeline_layout");
    }

    if (!sc->ovl->pipe) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = sc->ovl->vs;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = sc->ovl->fs;
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
        gp.layout = sc->ovl->pl;
        gp.renderPass = sc->rp;
        gp.subpass = 0;

        VkResult r = d->CreateGraphicsPipelines(d->Device, VK_NULL_HANDLE, 1, &gp, nullptr, &sc->ovl->pipe);
        if (r == VK_SUCCESS)
            SetName(d->Device, VK_OBJECT_TYPE_PIPELINE, uint64_t(sc->ovl->pipe), "mangohud_pipeline");

        VkDescriptorImageInfo di{};
        di.sampler = sc->ovl->sampler;
        di.imageView = ext->cache_view;
        di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet wd{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        wd.dstSet = sc->ovl->ds;
        wd.dstBinding = 0;
        wd.dstArrayElement = 0;
        wd.descriptorCount = 1;
        wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wd.pImageInfo = &di;

        d->UpdateDescriptorSets(d->Device, 1, &wd, 0, nullptr);
        if (r != VK_SUCCESS) return r;
    }

    return VK_SUCCESS;
}

VkResult OverlayVK::cmd_resources(const vkroots::VkDeviceDispatch* d,
                        swapchain_data* sc, uint32_t family, uint32_t image_count)
{
    if (sc->cmd_pool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = family;
        VkResult r = d->CreateCommandPool(d->Device, &pci, nullptr, &sc->cmd_pool);
        if (r != VK_SUCCESS) return r;
    }

    const size_t want = sc->images.size();
    bool need_alloc = (sc->cmd.size() != want);

    if (!need_alloc) {
        for (VkCommandBuffer cb : sc->cmd) {
            if (cb == VK_NULL_HANDLE) {
                need_alloc = true;
                break;
            }
        }
    }

    if (need_alloc) {
        VkResult r = d->ResetCommandPool(d->Device, sc->cmd_pool, 0);
        if (r != VK_SUCCESS) return r;

        sc->cmd.assign(want, VK_NULL_HANDLE);

        VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        ai.commandPool = sc->cmd_pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = (uint32_t)want;

        r = d->AllocateCommandBuffers(d->Device, &ai, sc->cmd.data());
        if (r != VK_SUCCESS) {
            sc->cmd.clear();
            return r;
        }
        for (VkCommandBuffer cb : sc->cmd) {
            // Required for dispatchable objects created inside a layer
            loader_data(d->Device, cb);
        }
    }

    sc->cmd_fences.resize(sc->cmd.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < sc->cmd.size(); i++) {
        if (sc->cmd_fences[i] == VK_NULL_HANDLE) {
            VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            VkResult r = d->CreateFence(d->Device, &fci, nullptr, &sc->cmd_fences[i]);
            if (r != VK_SUCCESS) {
                return r;
            }
            SetName(d->Device, VK_OBJECT_TYPE_FENCE, (uint64_t)sc->cmd_fences[i],
                    "mangohud_overlay_cmd_fence_%zu", i);
        }
    }

    sc->overlay_done.resize(image_count, VK_NULL_HANDLE);
    for (size_t i = 0; i < sc->overlay_done.size(); ++i) {
        auto& sema = sc->overlay_done[i];
        if (sema == VK_NULL_HANDLE) {
            VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            VkResult r = d->CreateSemaphore(d->Device, &si, nullptr, &sema);
            if (r != VK_SUCCESS) {
                return r;
            }
            SetName(d->Device, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)sema,
                    "mangohud_overlay_done_semaphore_%zu", i);
        }
    }

    if (acquire_sema == VK_NULL_HANDLE) {
        VkExportSemaphoreCreateInfo exportInfo{
            .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
            .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
        };

        VkSemaphoreCreateInfo sci{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &exportInfo,
        };

        VkResult r = d->CreateSemaphore(d->Device, &sci, nullptr, &acquire_sema);
        if (r != VK_SUCCESS) return r;
        SetName(d->Device, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)acquire_sema,
                "mangohud_acquire_semaphore");
    }

    return VK_SUCCESS;
}

VkShaderModule OverlayVK::make_shader(const vkroots::VkDeviceDispatch* d, VkDevice dev,
                                      const uint32_t* code, size_t codeSizeBytes)
{
    VkShaderModuleCreateInfo sci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sci.codeSize = codeSizeBytes;
    sci.pCode = code;

    VkShaderModule m = VK_NULL_HANDLE;
    if (d->CreateShaderModule(dev, &sci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

std::shared_ptr<swapchain_data> OverlayVK::get_swapchain_data(VkSwapchainKHR swap)
{
    std::lock_guard lock(swapchain_mtx);
    auto it = swapchains.find(swap);
    if (it != swapchains.end())
            return it->second;

    return nullptr;
}

VkResult OverlayVK::sample_dmabuf(const vkroots::VkDeviceDispatch* d, swapchain_data* sc,
                    uint32_t imageIndex, uint32_t w, uint32_t h, bool refresh_cache,
                    float offset_x_px, float offset_y_px)
{
    if (!ext || ext->cache_image == VK_NULL_HANDLE || ext->cache_view == VK_NULL_HANDLE)
        return VK_NOT_READY;

    VkCommandBuffer cb = sc->cmd[imageIndex];
    VkFence fence = sc->cmd_fences[imageIndex];

    VkResult r = d->WaitForFences(d->Device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (r != VK_SUCCESS) {
        return r;
    }

    r = d->ResetFences(d->Device, 1, &fence);
    if (r != VK_SUCCESS) {
        return r;
    }

    r = d->ResetCommandBuffer(cb, 0);
    if (r != VK_SUCCESS) {
        return r;
    }

    r = d->ResetCommandBuffer(cb, 0);
    if (r != VK_SUCCESS)
        return r;

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    r = d->BeginCommandBuffer(cb, &bi);
    if (r != VK_SUCCESS)
        return r;

    auto transition_image = [&](VkImage img,
                                VkImageLayout oldLayout,
                                VkImageLayout newLayout,
                                VkAccessFlags srcAccess,
                                VkAccessFlags dstAccess,
                                VkPipelineStageFlags srcStage,
                                VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.srcAccessMask = srcAccess;
        b.dstAccessMask = dstAccess;
        b.oldLayout = oldLayout;
        b.newLayout = newLayout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.baseMipLevel = 0;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount = 1;

        d->CmdPipelineBarrier(
            cb,
            srcStage,
            dstStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &b);
    };

    bool need_cache_init = (ext->cache_layout == VK_IMAGE_LAYOUT_UNDEFINED);
    bool do_cache_work = refresh_cache || need_cache_init;

    if (do_cache_work) {
        VkImageLayout cache_old = ext->cache_layout;
        VkAccessFlags cache_src_access = 0;
        VkPipelineStageFlags cache_src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        if (cache_old == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            cache_src_access = VK_ACCESS_SHADER_READ_BIT;
            cache_src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (cache_old == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            cache_src_access = VK_ACCESS_TRANSFER_WRITE_BIT;
            cache_src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        } else {
            // Treat anything else as UNDEFINED for safety.
            cache_old = VK_IMAGE_LAYOUT_UNDEFINED;
            cache_src_access = 0;
            cache_src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        }

        transition_image(
            ext->cache_image,
            cache_old,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            cache_src_access,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            cache_src_stage,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

        ext->cache_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        if (need_cache_init && !refresh_cache) {
            VkClearColorValue cc{};
            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;

            d->CmdClearColorImage(cb, ext->cache_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cc, 1, &range);
        }

        if (refresh_cache) {
            VkImageLayout src_old = ext->layout_ready ? ext->layout : VK_IMAGE_LAYOUT_UNDEFINED;

            transition_image(
                ext->image,
                src_old,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                0,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);

            ext->layout_ready = true;
            ext->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

            VkImageCopy region{};
            region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.srcSubresource.mipLevel = 0;
            region.srcSubresource.baseArrayLayer = 0;
            region.srcSubresource.layerCount = 1;
            region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.dstSubresource.mipLevel = 0;
            region.dstSubresource.baseArrayLayer = 0;
            region.dstSubresource.layerCount = 1;
            region.extent = { w, h, 1 };

            d->CmdCopyImage(
                cb,
                ext->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                ext->cache_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &region);
        }

        transition_image(
            ext->cache_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        ext->cache_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkImageMemoryBarrier swap_to_ca{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    swap_to_ca.srcAccessMask = 0;
    swap_to_ca.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    swap_to_ca.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    swap_to_ca.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swap_to_ca.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swap_to_ca.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swap_to_ca.image = sc->images[imageIndex];
    swap_to_ca.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swap_to_ca.subresourceRange.baseMipLevel = 0;
    swap_to_ca.subresourceRange.levelCount = 1;
    swap_to_ca.subresourceRange.baseArrayLayer = 0;
    swap_to_ca.subresourceRange.layerCount = 1;

    d->CmdPipelineBarrier(
        cb,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &swap_to_ca);

    VkRenderPassBeginInfo rbi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rbi.renderPass = sc->rp;
    rbi.framebuffer = sc->fb[imageIndex];
    rbi.renderArea.offset = { 0, 0 };
    rbi.renderArea.extent = sc->extent;

    d->CmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);

    d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sc->ovl->pipe);
    d->CmdBindDescriptorSets(
        cb,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        sc->ovl->pl,
        0,
        1,
        &sc->ovl->ds,
        0,
        nullptr);

    OverlayPushConsts pc{};
    pc.dstExtent[0] = (float)sc->extent.width;
    pc.dstExtent[1] = (float)sc->extent.height;
    pc.srcExtent[0] = (float)w;
    pc.srcExtent[1] = (float)h;
    pc.offsetPx[0] = offset_x_px;
    pc.offsetPx[1] = offset_y_px;
    pc.transfer_function = convert_colors_vk(sc->format, sc->colorspace);

    d->CmdPushConstants(
        cb,
        sc->ovl->pl,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        (uint32_t)sizeof(pc),
        &pc);

    d->CmdDraw(cb, 3, 1, 0, 0);

    d->CmdEndRenderPass(cb);

    VkImageMemoryBarrier swap_to_present = swap_to_ca;
    swap_to_present.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    swap_to_present.dstAccessMask = 0;
    swap_to_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swap_to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    d->CmdPipelineBarrier(
        cb,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &swap_to_present);

    r = d->EndCommandBuffer(cb);
    return r;
}

VkResult OverlayVK::semaphores(const vkroots::VkDeviceDispatch* d, swapchain_data* sc,
                    uint32_t img_idx, VkPresentInfoKHR* pPresentInfo,
                    VkQueue queue, bool frame_ready)
{
    VkSemaphore overlay_done = sc->overlay_done[img_idx];

    std::vector<VkSemaphore> waits;
    std::vector<VkPipelineStageFlags> wait_stages;
    waits.reserve(pPresentInfo->waitSemaphoreCount + 1);
    wait_stages.reserve(pPresentInfo->waitSemaphoreCount + 1);

    for (uint32_t i = 0; i < pPresentInfo->waitSemaphoreCount; i++) {
        waits.push_back(pPresentInfo->pWaitSemaphores[i]);
        wait_stages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    }

    VkSemaphore new_release_sema = VK_NULL_HANDLE;

    if (frame_ready) {
        VkExportSemaphoreCreateInfo exportInfo{
            .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
        };

        VkSemaphoreCreateInfo sci{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &exportInfo,
            .flags = 0,
        };

        VkResult r = d->CreateSemaphore(d->Device, &sci, nullptr, &new_release_sema);

        if (r != VK_SUCCESS) {
            SPDLOG_ERROR("CreateSemaphore {}", vk_result_string(r));
            new_release_sema = VK_NULL_HANDLE;
            frame_ready = false;
        } else {
            SetName(d->Device, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)new_release_sema, "mangohud_release_semaphore");
        }
    }

    VkSemaphore signals[2];
    uint32_t signal_count = 0;
    signals[signal_count++] = overlay_done;
    if (new_release_sema != VK_NULL_HANDLE) {
        signals[signal_count++] = new_release_sema;
    }

    VkSubmitInfo sub{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkCommandBuffer cb = sc->cmd[img_idx];
    sub.waitSemaphoreCount = (uint32_t)waits.size();
    sub.pWaitSemaphores = waits.data();
    sub.pWaitDstStageMask = wait_stages.data();
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cb;
    sub.signalSemaphoreCount = signal_count;
    sub.pSignalSemaphores = signals;

    VkFence submit_fence = sc->cmd_fences[img_idx];
    VkResult sr = d->QueueSubmit(queue, 1, &sub, submit_fence);

    if (sr != VK_SUCCESS) {
        if (new_release_sema != VK_NULL_HANDLE) {
            d->DestroySemaphore(d->Device, new_release_sema, nullptr);
        }
        return sr;
    }

    if (new_release_sema != VK_NULL_HANDLE) {
        release_sema = new_release_sema;

        if (ipc) {
            int release_fd = -1;
            VkSemaphoreGetFdInfoKHR gi{ VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR };
            gi.semaphore = release_sema;
            gi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

            VkResult gr = d->GetSemaphoreFdKHR(d->Device, &gi, &release_fd);
            if (gr == VK_SUCCESS && release_fd >= 0) {
                ipc->queue_fence(release_fd);
            }
        }
    }

    pPresentInfo->waitSemaphoreCount = 1;
    pPresentInfo->pWaitSemaphores = &sc->overlay_done[img_idx];

    return VK_SUCCESS;
}

uint32_t OverlayVK::find_mem_type(const vkroots::VkDeviceDispatch* d, const VkImage image)
{
    VkMemoryRequirements req{};
    d->GetImageMemoryRequirements(d->Device, image, &req);

    VkPhysicalDeviceMemoryProperties mp{};
    d->pPhysicalDeviceDispatch->pInstanceDispatch->GetPhysicalDeviceMemoryProperties(d->PhysicalDevice, &mp);
    uint32_t mt = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((req.memoryTypeBits & (1u << i)) == 0) {
            continue;
        }

        VkMemoryPropertyFlags flags = mp.memoryTypes[i].propertyFlags;

        if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0) {
            continue;
        }
    #ifdef VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD
        if ((flags & VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD) != 0) {
            continue;
        }
    #endif
        mt = i;
        break;
    }

    return mt;
}

VkResult OverlayVK::import_dmabuf(const vkroots::VkDeviceDispatch* d,
                       const VkAllocationCallbacks* alloc) {
    auto tmp_ext = std::make_unique<dmabuf>();
    VkFormat fmt = VK_FORMAT_B8G8R8A8_SRGB;
    if (fmt == VK_FORMAT_UNDEFINED)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;

    VkSubresourceLayout plane{};
    plane.offset = ipc->fdinfo.dmabuf_offset;
    plane.rowPitch = ipc->fdinfo.stride;
    plane.size = 0;

    VkImageDrmFormatModifierExplicitCreateInfoEXT drm_explicit{
        VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT
    };
    drm_explicit.drmFormatModifier = ipc->fdinfo.modifier;
    drm_explicit.drmFormatModifierPlaneCount = 1;
    drm_explicit.pPlaneLayouts = &plane;

    VkExternalMemoryImageCreateInfo ext_img{
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO
    };
    ext_img.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    ext_img.pNext = &drm_explicit;

    VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.pNext = &ext_img;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent.width  = ipc->fdinfo.w;
    ici.extent.height = ipc->fdinfo.h;
    ici.extent.depth  = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    tmp_ext->d = d;
    VkResult r = d->CreateImage(d->Device, &ici, alloc, &tmp_ext->image);
    if (r != VK_SUCCESS) {
        SPDLOG_ERROR("CreateImage {}", vk_result_string(r));
        return r;
    }
    SetName(d->Device, VK_OBJECT_TYPE_IMAGE, (uint64_t)tmp_ext->image,
            "mangohud_dmabuf_image");

    VkMemoryRequirements req{};
    d->GetImageMemoryRequirements(d->Device, tmp_ext->image, &req);
    uint32_t mt = find_mem_type(d, tmp_ext->image);
    if (mt == UINT32_MAX)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;

    int import_fd = ::dup(ipc->fdinfo.gbm_fd);
    if (import_fd < 0)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkImportMemoryFdInfoKHR import{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR };
    import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    import.fd = import_fd;

    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.pNext = &import;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mt;

    r = d->AllocateMemory(d->Device, &mai, alloc, &tmp_ext->mem);
    if (r != VK_SUCCESS) {
        ::close(import_fd);
        SPDLOG_ERROR("AllocateMemory {}", vk_result_string(r));
        return r;
    }

    r = d->BindImageMemory(d->Device, tmp_ext->image, tmp_ext->mem, 0);

    if (r != VK_SUCCESS)
        return r;

    SetName(d->Device, VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)tmp_ext->mem,
            "mangohud_dmabuf_memory");

    VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image = tmp_ext->image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;

    VkImageCreateInfo cici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    cici.imageType = VK_IMAGE_TYPE_2D;
    cici.format = fmt;
    cici.extent.width = ipc->fdinfo.w;
    cici.extent.height = ipc->fdinfo.h;
    cici.extent.depth = 1;
    cici.mipLevels = 1;
    cici.arrayLayers = 1;
    cici.samples = VK_SAMPLE_COUNT_1_BIT;
    cici.tiling = VK_IMAGE_TILING_OPTIMAL;
    cici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    cici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    cici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    r = d->CreateImage(d->Device, &cici, alloc, &tmp_ext->cache_image);
    if (r != VK_SUCCESS)
        return r;

    SetName(d->Device, VK_OBJECT_TYPE_IMAGE, (uint64_t)tmp_ext->cache_image,
            "mangohud_cache_image");

    VkMemoryRequirements creq{};
    d->GetImageMemoryRequirements(d->Device, tmp_ext->cache_image, &creq);

    mt = find_mem_type(d, tmp_ext->cache_image);

    if (mt == UINT32_MAX)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;

    VkMemoryAllocateInfo cmai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    cmai.allocationSize = creq.size;
    cmai.memoryTypeIndex = mt;

    r = d->AllocateMemory(d->Device, &cmai, alloc, &tmp_ext->cache_mem);
    if (r != VK_SUCCESS)
        return r;

    r = d->BindImageMemory(d->Device, tmp_ext->cache_image, tmp_ext->cache_mem, 0);
    if (r != VK_SUCCESS)
        return r;

    SetName(d->Device, VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)tmp_ext->cache_mem,
            "mangohud_cache_memory");

    VkImageViewCreateInfo cvci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    cvci.image = tmp_ext->cache_image;
    cvci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    cvci.format = fmt;
    cvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    cvci.subresourceRange.levelCount = 1;
    cvci.subresourceRange.layerCount = 1;

    r = d->CreateImageView(d->Device, &cvci, alloc, &tmp_ext->cache_view);
    if (r != VK_SUCCESS)
        return r;

    SetName(d->Device, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)tmp_ext->cache_view,
            "mangohud_cache_view");

    tmp_ext->cache_format = fmt;

    r = d->CreateImageView(d->Device, &vci, alloc, &tmp_ext->view);
    if (r != VK_SUCCESS)
        return r;

    SetName(d->Device, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)tmp_ext->view,
            "mangohud_dmabuf_view");

    tmp_ext->valid = true;
    tmp_ext->fd = ipc->fdinfo.gbm_fd;
    tmp_ext->width = ipc->fdinfo.w;
    tmp_ext->height = ipc->fdinfo.h;
    if (tmp_ext->width == 0 || tmp_ext->height == 0)
        return VK_NOT_READY;
    tmp_ext->fourcc = ipc->fdinfo.fourcc;
    tmp_ext->modifier = ipc->fdinfo.modifier;
    tmp_ext->stride = ipc->fdinfo.stride;
    tmp_ext->offset = ipc->fdinfo.dmabuf_offset;
    tmp_ext->plane_size = ipc->fdinfo.plane_size;
    tmp_ext->format = fmt;
    tmp_ext->layout_ready = false;
    tmp_ext->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    ext = std::move(tmp_ext);

    ipc->needs_import.store(false);

    return VK_SUCCESS;
}
