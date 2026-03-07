#include "vulkan.h"
#include "layer.h"
#include "../utils/mesa/os_time.h"

bool OverlayVK::draw(VkSwapchainKHR swapchain, uint32_t img_idx, VkQueue q, VkPresentInfoKHR pi)
{
    queue = q;
    sc = layer->get_swapchain_data(swapchain);
    auto d = sc->d;
    if (!sc) return false;
    if (!layer->ipc->connected.load()) return false;
    if (dmabufs.empty()) return false;
    if (current_slot < 0) current_slot = layer->ipc->next_frame();

    if (copy_dmabuf_to_cache(queue, img_idx, pi) != VK_SUCCESS)
        return false;

    return true;
}

std::vector<int> OverlayVK::init_dmabufs(Fdinfo& fdinfo) {
    std::lock_guard lock(m);
    sc->d->DeviceWaitIdle(sc->d->Device);
    last_slot = -1;
    dmabufs.clear();
    std::vector<int> semaphores;
    for (auto [i, fd] : enumerate(fdinfo.dmabuf_buffer)) {
        dmabufs.push_back(std::make_shared<dmabuf_ext>(sc->d));
        import_dmabuf(dmabufs.back().get(), fd, fdinfo);
        cache_descriptor_set(dmabufs.back());
    }

    return semaphores;
}

void OverlayVK::cache_descriptor_set(std::shared_ptr<dmabuf_ext>& buf) {
    auto cached = buf->cached;
    cached->ovl_res = layer->ovl_res;
    if (cached->ds)
        return;

    VkDescriptorSetAllocateInfo as{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    as.descriptorPool = cached->ovl_res->dp;
    as.descriptorSetCount = 1;
    as.pSetLayouts = &cached->ovl_res->dsl;

    VkResult r = sc->d->AllocateDescriptorSets(sc->d->Device, &as, &cached->ds);
    if (r == VK_SUCCESS) {
        layer->SetName(sc->d->Device, VK_OBJECT_TYPE_DESCRIPTOR_SET, uint64_t(cached->ds), "mangohud_cached_descriptor_set");
    } else {
        SPDLOG_ERROR("AllocateDescriptorSets {}", string_VkResult(r));
    }

    VkDescriptorImageInfo di{};
    di.sampler = cached->ovl_res->sampler;
    di.imageView = cached->view;
    di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet wd{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    wd.dstSet = cached->ds;
    wd.dstBinding = 0;
    wd.dstArrayElement = 0;
    wd.descriptorCount = 1;
    wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wd.pImageInfo = &di;

    sc->d->UpdateDescriptorSets(sc->d->Device, 1, &wd, 0, nullptr);
}

uint32_t OverlayVK::find_mem_type_import(VkImage image, int fd) {
    VkImageMemoryRequirementsInfo2 info2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
        .image = image,
    };

    VkMemoryRequirements2 req2{ .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
    sc->d->GetImageMemoryRequirements2KHR(sc->d->Device, &info2, &req2);
    uint32_t imgBits = req2.memoryRequirements.memoryTypeBits;

    VkMemoryFdPropertiesKHR fdProps{ VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR };
    VkResult r = sc->d->GetMemoryFdPropertiesKHR(sc->d->Device,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fdProps);
    if (r != VK_SUCCESS)
        return UINT32_MAX;

    uint32_t type_bits = imgBits & fdProps.memoryTypeBits;

    VkPhysicalDeviceMemoryProperties mp{};
    sc->d->pPhysicalDeviceDispatch->pInstanceDispatch
        ->GetPhysicalDeviceMemoryProperties(sc->d->PhysicalDevice, &mp);

    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) == 0)
            continue;
        return i;
    }
    return UINT32_MAX;
}

uint32_t OverlayVK::find_mem_type_image(uint32_t type_bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp{};
    sc->d->pPhysicalDeviceDispatch->pInstanceDispatch
        ->GetPhysicalDeviceMemoryProperties(sc->d->PhysicalDevice, &mp);

    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) == 0)
            continue;
        if ((mp.memoryTypes[i].propertyFlags & want) != want)
            continue;
        return i;
    }
    return UINT32_MAX;
}

VkResult OverlayVK::import_dmabuf(dmabuf_ext* buf, unique_fd& fd, Fdinfo& fdinfo) {
    auto d = sc->d;

    VkSubresourceLayout plane{};
    plane.offset = fdinfo.dmabuf_offset;
    plane.rowPitch = fdinfo.stride;
    plane.size = 0;

    VkImageDrmFormatModifierExplicitCreateInfoEXT drm_explicit{
        VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT
    };
    drm_explicit.drmFormatModifier = fdinfo.modifier;
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
    ici.extent.width  = fdinfo.w;
    ici.extent.height = fdinfo.h;
    ici.extent.depth  = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    buf->d = d;
    VkResult r = d->CreateImage(d->Device, &ici, nullptr, &buf->image);
    if (r != VK_SUCCESS) {
        SPDLOG_ERROR("CreateImage {}", string_VkResult(r));
        SPDLOG_DEBUG(
            "dmabuf import: fourcc=0x{:08x} '{}' -> VkFormat={} ({}) colorspace={} transfer_function={} modifier=0x{:016x}",
            fdinfo.fourcc,
            fourcc_to_string(fdinfo.fourcc),
            string_VkFormat(fmt),
            static_cast<uint32_t>(fmt),
            string_VkColorSpaceKHR(sc->colorspace),
            convert_colors_vk(sc->format, sc->colorspace),
            static_cast<unsigned long long>(fdinfo.modifier)
        );

        SPDLOG_DEBUG(
            "plane[0]: offset={} rowPitch={} w={} h={} usage=0x{:x}",
            static_cast<unsigned long long>(plane.offset),
            static_cast<unsigned long long>(plane.rowPitch),
            fdinfo.w,
            fdinfo.h,
            static_cast<unsigned>(ici.usage)
        );
    return r;
    }

    static uint64_t idx = 0;
    layer->SetName(d->Device, VK_OBJECT_TYPE_IMAGE, (uint64_t)buf->image,
            "mangohud_dmabuf_image_%" PRIu64, ++idx);

    ici.pNext = nullptr;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    r = d->CreateImage(d->Device, &ici, nullptr, &buf->cached->image);
    if (r != VK_SUCCESS) {
        SPDLOG_ERROR("CreateImage {}", string_VkResult(r));
        return r;
    }

    layer->SetName(d->Device, VK_OBJECT_TYPE_IMAGE, (uint64_t)buf->cached->image,
            "mangohud_cache_image_%i", idx);

    VkMemoryDedicatedRequirements ded_req{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    VkMemoryRequirements2 req2{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    req2.pNext = &ded_req;

    VkImageMemoryRequirementsInfo2 info2{VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};
    info2.image = buf->image;

    d->GetImageMemoryRequirements2KHR(d->Device, &info2, &req2);

    int import_fd = dup(fd.get());
    if (import_fd < 0)
        return VK_ERROR_INITIALIZATION_FAILED;

    uint32_t mt = find_mem_type_import(buf->image, import_fd);
    if (mt == UINT32_MAX)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;

    VkImportMemoryFdInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
    import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    import.fd = import_fd;

    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.image = buf->image;

    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req2.memoryRequirements.size;
    mai.memoryTypeIndex = mt;

    if (ded_req.requiresDedicatedAllocation) {
        dedicated.pNext = &import;
        mai.pNext = &dedicated;
    } else {
        mai.pNext = &import;
    }

    r = d->AllocateMemory(d->Device, &mai, nullptr, &buf->mem);
    if (r != VK_SUCCESS) {
        SPDLOG_ERROR("AllocateMemory {}", string_VkResult(r));
        return r;
    }

    r = d->BindImageMemory(d->Device, buf->image, buf->mem, 0);
    if (r != VK_SUCCESS) {
        SPDLOG_ERROR("BindImageMemory {}", string_VkResult(r));
        return r;
    }

    layer->SetName(d->Device, VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)buf->mem,
            "mangohud_dmabuf_memory_%" PRIu64, ++idx);

    VkMemoryRequirements req{};
    d->GetImageMemoryRequirements(d->Device, buf->cached->image, &req);

    mai.pNext = nullptr;
    mai.allocationSize = req.size;

    uint32_t mt_cached = find_mem_type_image(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mt_cached == UINT32_MAX)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;

    mai.memoryTypeIndex = mt_cached;

    r = d->AllocateMemory(d->Device, &mai, nullptr, &buf->cached->mem);
    if (r != VK_SUCCESS) {
        SPDLOG_ERROR("AllocateMemory cached {}", string_VkResult(r));
        return r;
    }

    r = d->BindImageMemory(d->Device, buf->cached->image, buf->cached->mem, 0);
    if (r != VK_SUCCESS) {
        SPDLOG_ERROR("BindImageMemory cached {}", string_VkResult(r));
        return r;
    }

    layer->SetName(d->Device, VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)buf->cached->mem,
            "mangohud_cache_memory_%i", idx);

    VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image = buf->image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;

    r = d->CreateImageView(d->Device, &vci, nullptr, &buf->view);
    if (r != VK_SUCCESS)
        return r;

    vci.image = buf->cached->image;
    d->CreateImageView(d->Device, &vci, nullptr, &buf->cached->view);

    layer->SetName(d->Device, VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)buf->cached->view,
            "mangohud_cache_view_%i", idx);

    buf->cached->format = fmt;
    buf->valid = true;
    buf->width = fdinfo.w;
    buf->height = fdinfo.h;
    if (buf->width == 0 || buf->height == 0)
        return VK_NOT_READY;
    buf->fourcc = fdinfo.fourcc;
    buf->modifier = fdinfo.modifier;
    buf->stride = fdinfo.stride;
    buf->offset = fdinfo.dmabuf_offset;
    buf->plane_size = fdinfo.plane_size;
    buf->format = fmt;
    buf->layout_ready = false;
    buf->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    buf->needs_import = false;

    return VK_SUCCESS;
}

static void layout_to_access_stage(VkImageLayout layout,
                                  VkAccessFlags& access,
                                  VkPipelineStageFlags& stage)
{
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            access = 0;
            stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;

        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            access = VK_ACCESS_TRANSFER_READ_BIT;
            stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;

        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            access = VK_ACCESS_TRANSFER_WRITE_BIT;
            stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;

        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            access = VK_ACCESS_SHADER_READ_BIT;
            stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            break;

        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;

        case VK_IMAGE_LAYOUT_GENERAL:
            access =
                VK_ACCESS_TRANSFER_READ_BIT |
                VK_ACCESS_TRANSFER_WRITE_BIT |
                VK_ACCESS_SHADER_READ_BIT |
                VK_ACCESS_SHADER_WRITE_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            stage =
                VK_PIPELINE_STAGE_TRANSFER_BIT |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;

        default:
            access = 0;
            stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            break;
    }
}

void OverlayVK::transition_image(VkCommandBuffer cmd, VkImage image,
                                 VkImageLayout old_layout, VkImageLayout new_layout)
{
    if (old_layout == new_layout)
        return;

    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = old_layout;
    b.newLayout = new_layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage = 0;
    VkPipelineStageFlags dst_stage = 0;

    layout_to_access_stage(old_layout, b.srcAccessMask, src_stage);
    layout_to_access_stage(new_layout, b.dstAccessMask, dst_stage);

    sc->d->CmdPipelineBarrier(cmd,
                          src_stage, dst_stage,
                          0,
                          0, nullptr,
                          0, nullptr,
                          1, &b);
}

void OverlayVK::cache_to_transfer_dst(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout)
{
    if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        return;

    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = old_layout;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage = 0;
    layout_to_access_stage(old_layout, b.srcAccessMask, src_stage);

    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    sc->d->CmdPipelineBarrier(cmd,
                              src_stage, dst_stage,
                              0, 0, nullptr, 0, nullptr, 1, &b);
}

void OverlayVK::cache_to_shader_read(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout)
{
    if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        return;

    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = old_layout;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage = 0;
    layout_to_access_stage(old_layout, b.srcAccessMask, src_stage);

    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    sc->d->CmdPipelineBarrier(cmd,
                              src_stage, dst_stage,
                              0, 0, nullptr, 0, nullptr, 1, &b);
}

VkResult OverlayVK::copy_dmabuf_to_cache(VkQueue queue, int img_idx, VkPresentInfoKHR pi)
{
    int slot = -1;
    bool refresh_cache = false;

    if (current_slot >= 0) {
        slot = current_slot;
        refresh_cache = (slot != last_slot);
    } else if (last_slot >= 0) {
        slot = last_slot;
    }

    if (slot >= 0 && dmabufs[slot] && dmabufs[slot]->cached && !dmabufs[slot]->cached->valid) {
        refresh_cache = true;
    }

    if (slot < 0)
        return VK_NOT_READY;

    auto& buf = dmabufs[slot];
    if (!buf || !buf->cached || buf->cached->image == VK_NULL_HANDLE)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (refresh_cache && buf->image == VK_NULL_HANDLE)
        return VK_ERROR_INITIALIZATION_FAILED;

    auto& fence = layer->ovl_res->cmd_fences[img_idx];
    auto& cmd = layer->ovl_res->cmd[img_idx];

    VkResult r = sc->d->WaitForFences(sc->d->Device, 1, &fence, VK_FALSE, 100'000'000);
    if (r != VK_SUCCESS) return r;
    r = sc->d->ResetFences(sc->d->Device, 1, &fence);
    if (r != VK_SUCCESS) return r;

    r = sc->d->ResetCommandBuffer(cmd, 0);
    if (r != VK_SUCCESS) return r;

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    r = sc->d->BeginCommandBuffer(cmd, &bi);
    if (r != VK_SUCCESS) return r;

    if (refresh_cache)
    {
        transition_image(cmd, buf->image, buf->layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        buf->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        cache_to_transfer_dst(cmd, buf->cached->image, buf->cached->layout);
        buf->cached->layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        VkImageCopy region{};
        region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.srcSubresource.layerCount = 1;
        region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.dstSubresource.layerCount = 1;
        region.extent.width = buf->width;
        region.extent.height = buf->height;
        region.extent.depth = 1;

        sc->d->CmdCopyImage(cmd,
                        buf->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        buf->cached->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1, &region);

        cache_to_shader_read(cmd, buf->cached->image, buf->cached->layout);
        buf->cached->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        last_slot = slot;
        buf->cached->valid = true;
    } else {
        if (buf->cached->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            transition_image(cmd, buf->cached->image, buf->cached->layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            buf->cached->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }

    VkRenderPassBeginInfo rpbi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpbi.renderPass = sc->rp;
    rpbi.framebuffer = sc->fb[img_idx];
    rpbi.renderArea.offset = { 0, 0 };
    rpbi.renderArea.extent = sc->extent;

    sc->d->CmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    sc->d->CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sc->pipe);

    sc->d->CmdBindDescriptorSets(cmd,
                             VK_PIPELINE_BIND_POINT_GRAPHICS,
                             layer->ovl_res->pl,
                             0, 1, &buf->cached->ds,
                             0, nullptr);

    OverlayPushConsts pc{};
    pc.dstExtent[0] = (float)sc->extent.width;
    pc.dstExtent[1] = (float)sc->extent.height;
    pc.srcExtent[0] = (float)buf->width;
    pc.srcExtent[1] = (float)buf->height;
    pc.transfer_function = convert_colors_vk(sc->format, sc->colorspace);

    sc->d->CmdPushConstants(cmd, layer->ovl_res->pl, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    sc->d->CmdDraw(cmd, 3, 1, 0, 0);
    sc->d->CmdEndRenderPass(cmd);

    r = sc->d->EndCommandBuffer(cmd);
    if (r != VK_SUCCESS) return r;

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

    static thread_local std::vector<VkPipelineStageFlags> wait_stages;
    wait_stages.assign(pi.waitSemaphoreCount, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    si.waitSemaphoreCount = pi.waitSemaphoreCount;
    si.pWaitSemaphores = pi.pWaitSemaphores;
    si.pWaitDstStageMask = wait_stages.data();

    VkSemaphore signals[2];
    uint32_t signal_count = 0;

    if (refresh_cache)
        signals[signal_count++] = buf->cached->semaphore;

    signals[signal_count++] = layer->ovl_res->overlay_done[img_idx];

    si.signalSemaphoreCount = signal_count;
    si.pSignalSemaphores = signals;

    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    r = sc->d->QueueSubmit(queue, 1, &si, fence);
    if (r != VK_SUCCESS)
        SPDLOG_ERROR("QueueSubmit {}", string_VkResult(r));

    if (refresh_cache) {
        int fd = -1;
        VkSemaphoreGetFdInfoKHR gi{ VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR };
        gi.semaphore = buf->cached->semaphore;
        gi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

        VkResult gr = sc->d->GetSemaphoreFdKHR(sc->d->Device, &gi, &fd);
        if (gr == VK_SUCCESS && fd >= 0) {
            layer->ipc->frame_ready(slot, fd);
        }
    }

    current_slot = -1;
    return r;
}

cached_image::~cached_image() {
    if (!d) return;

    if (view) {
        d->DestroyImageView(d->Device, view, nullptr);
        view = VK_NULL_HANDLE;
    }

    if (image) {
        d->DestroyImage(d->Device, image, nullptr);
        image = VK_NULL_HANDLE;
    }

    if (mem) {
        d->FreeMemory(d->Device, mem, nullptr);
        mem = VK_NULL_HANDLE;
    }

    if (semaphore) {
        d->DestroySemaphore(d->Device, semaphore, nullptr);
        semaphore = VK_NULL_HANDLE;
    }

    if (ds) {
        d->FreeDescriptorSets(d->Device, ovl_res->dp, 1, &ds);
        ds = VK_NULL_HANDLE;
    }
}
