#include <stdexcept>
#include <string>
#include <fcntl.h>
#include "vulkan_ctx.h"
#include <gbm.h>
#include <drm/drm_fourcc.h>
#include "unistd.h"
#include "mesa/os_time.h"

vkb::PhysicalDevice VkCtx::pick_device(vkb::Instance instance) {
    vkb::PhysicalDeviceSelector selector{instance};
    selector.set_minimum_version(1, 3);
    selector.require_present(false);

    VkPhysicalDeviceVulkan13Features f13{};
    f13.dynamicRendering = VK_TRUE;
    selector.set_required_features_13(f13);

    selector.add_required_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    selector.add_required_extension(VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME);
    selector.add_required_extension(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
    selector.add_required_extension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    selector.add_required_extension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);

    if (use_dmabuf) {
        selector.add_required_extension(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
        // Some GPUs will not support this and so we can't use dmabuf
        // TODO add opaque fd path to vulkan layer for this case
        selector.add_required_extension(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
    }

    auto devicesRet = selector.select_devices();
    if (!devicesRet) {
        SPDLOG_ERROR("Selector failed\n");
        return vkb::PhysicalDevice();
    }

    for (const auto& dev : devicesRet.value()) {
        VkPhysicalDeviceDrmPropertiesEXT drm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        props2.pNext = &drm;
        vkGetPhysicalDeviceProperties2(dev.physical_device, &props2);

        if (!drm.hasRender)
            continue;

        if (drm.renderMinor != renderMinor)
            continue;

        return dev;
    }

    use_dmabuf = false;
    return vkb::PhysicalDevice();
}


void VkCtx::init(bool enableValidation = true) {
    vkb::InstanceBuilder ib;
    ib.set_app_name("mangohud-server")
        .set_engine_name("MangoHud")
        .require_api_version(1, 3, 0);

    if (enableValidation) {
        ib.request_validation_layers(true)
        .use_default_debug_messenger();
    }

    auto instRet = ib.build();
    auto vkbInstance_ = instRet.value();
    instance = vkbInstance_.instance;
    debugMessenger = enableValidation ? vkbInstance_.debug_messenger : VK_NULL_HANDLE;
    vkb::PhysicalDevice vkb_device;
    vkb_device = pick_device(vkbInstance_);
    if (!vkb_device.physical_device)
        vkb_device = pick_device(vkbInstance_);

    if (!vkb_device.physical_device) {
        SPDLOG_ERROR("can't find GPU {}, bailing", renderMinor);
        exit(1);
    }

    vkb::DeviceBuilder db{vkb_device};
    auto vkbDevice_ = db.build().value();
    device = vkbDevice_.device;
    physicalDevice = vkbDevice_.physical_device;
    auto gq = vkbDevice_.get_queue(vkb::QueueType::graphics);
    graphicsQueue = gq.value();
    auto gqfi = vkbDevice_.get_queue_index(vkb::QueueType::graphics);
    graphicsQueueFamilyIndex = gqfi.value();

    pfn_vkGetMemoryFdPropertiesKHR = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(device, "vkGetMemoryFdPropertiesKHR");
    pfn_vkGetSemaphoreFdKHR = (PFN_vkGetSemaphoreFdKHR)vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR");

    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device, &fi, nullptr, &fence);
}

int VkCtx::phys_fd() {
    if (phys_fd_ >= 0)
        return phys_fd_;

    VkPhysicalDeviceDrmPropertiesEXT drm{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT,
    };
    VkPhysicalDeviceProperties2 props2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &drm,
    };
    vkGetPhysicalDeviceProperties2(physicalDevice, &props2);
    if (!drm.hasRender)
        return -1;

    std::string path = "/dev/dri/renderD" + std::to_string(drm.renderMinor);
    printf("render path %s\n", path.c_str());
    phys_fd_ = open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (phys_fd_ < 0) throw std::runtime_error("Failed to open " + path);
    return phys_fd_;
}

uint32_t VkCtx::find_mem_type(uint32_t bits, VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &mp);

    // TODO this is probably too much, we can likely strip this down
    uint32_t best = UINT32_MAX;
    uint32_t bestScore = 0xffffffffu;

    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((bits & (1u << i)) == 0) continue;

        VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
        if ((f & required) != required) continue;

        if (f & VK_MEMORY_PROPERTY_PROTECTED_BIT) continue;

        VkMemoryPropertyFlags extra = f & ~required;
        uint32_t score = __builtin_popcount((uint32_t)extra);

        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }

    if (best != UINT32_MAX) return best;

    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((bits & (1u << i)) == 0) continue;
        if (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_PROTECTED_BIT) continue;
        return i;
    }

    return UINT32_MAX;
}

uint32_t VkCtx::compatible_bits_for_dmabuf_import(VkImage image, int import_fd) {
    VkImageMemoryRequirementsInfo2 info2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
        .image = image,
    };
    VkMemoryRequirements2 req2{ .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
    vkGetImageMemoryRequirements2(device, &info2, &req2);
    uint32_t imgBits = req2.memoryRequirements.memoryTypeBits;

    VkMemoryFdPropertiesKHR fdProps{ VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR };
    VkResult r = pfn_vkGetMemoryFdPropertiesKHR(
        device,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        import_fd,
        &fdProps
    );
    if (r != VK_SUCCESS) return 0;

    return imgBits & fdProps.memoryTypeBits;
}

void VkCtx::init_client(clientRes* r) {
    if (!r->w) r->w = 500;
    if (!r->h) r->h = 500;
    if (!device)
        return;

    r->device = device;
    r->server_render_minor = renderMinor;

    if (use_dmabuf) {
        if (!create_gbm_buffer(r)) {
            fprintf(stderr, "failed to create gbm buffer\n");
            fprintf(stderr, "we can't create dmabuf, bailing\n");
            return;
        }


        if (!create_dmabuf(r)) {
            fprintf(stderr, "failed to create dmabuf image\n");
            fprintf(stderr, "bailing\n");
            return;
        }
    }

    if (!create_src(r)) {
        fprintf(stderr, "failed to create src image\n");
        fprintf(stderr, "we can't create dmabuf, bailing\n");
        return;
    }

    if (!create_opaque(r))
        fprintf(stderr, "failed to create opaque fd\n");

    // TODO run imgui->draw once to calculate the initial width/height
    // this is currently a double lock so we need to redesign this a bit
    // we want to do this so we don't end up always pushing two dmabufs on connect
    // and when the the overlay changes
    // imgui->draw(r);

    r->send_dmabuf = true;
    r->initialized = true;
}

bool VkCtx::create_src(clientRes* r) {
    create_image(NULL, r, r->src,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_TILING_OPTIMAL, 0);
    allocate_memory(
        r->src,
        r->src_mem,
        r,
        nullptr,
        false,           // external
        false,           // import_dmabuf
        0                // handleType (ignored)
    );
    create_view(r->src, r->src_mem, r->src_view, fmt);
    return true;
}

bool VkCtx::create_dmabuf(clientRes* r) {
    VkSubresourceLayout plane0{
        .offset = r->gbm.offset,
        .size = 0,
        .rowPitch = r->gbm.stride,
        .arrayPitch = 0,
        .depthPitch = 0,
    };

    VkImageDrmFormatModifierExplicitCreateInfoEXT drmExplicit{
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
        .drmFormatModifier = r->gbm.modifier,
        .drmFormatModifierPlaneCount = 1,
        .pPlaneLayouts = &plane0,
    };

    create_image(&drmExplicit, r, r->dmabuf,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
    allocate_memory(
        r->dmabuf,
        r->dmabuf_mem,
        r,
        nullptr,         // allocSize
        true,            // external
        true,            // import_dmabuf
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
    );

    create_view(r->dmabuf, r->dmabuf_mem, r->dmabuf_view, fmt);
    return true;
}

bool VkCtx::create_opaque(clientRes* r) {
    create_image(NULL, r, r->opaque,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_IMAGE_TILING_OPTIMAL, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
    allocate_memory(
        r->opaque,
        r->opaque_mem,
        r,
        &r->opaque_size,
        true,            // external
        false,           // import_dmabuf
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT
    );
    create_view(r->opaque, r->opaque_mem, r->opaque_view, fmt);
    r->opaque_fd = export_opaquefd(r->opaque_mem);
    return true;
}

int VkCtx::export_opaquefd(VkDeviceMemory mem){
    VkMemoryGetFdInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .memory = mem,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
    };
    int fd = -1;
    PFN_vkGetMemoryFdKHR pfn_vkGetMemoryFdKHR =
        (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR");

    VkResult r = pfn_vkGetMemoryFdKHR(device, &info, &fd);
    if (r != VK_SUCCESS) return -1;
    return fd;
}

bool VkCtx::create_gbm_buffer(clientRes* r) {
    r->gbm.fourcc = DRM_FORMAT_ARGB8888;
    r->gbm.dev = gbm_create_device(phys_fd());
    const uint64_t linear = DRM_FORMAT_MOD_LINEAR;

    r->gbm.bo = gbm_bo_create_with_modifiers(r->gbm.dev, r->w, r->h, r->gbm.fourcc, &linear, 1);
    if (!r->gbm.bo) {
        r->gbm.bo = gbm_bo_create(r->gbm.dev, r->w, r->h, r->gbm.fourcc, GBM_BO_USE_RENDERING);
        if (!r->gbm.bo) {
            fprintf(stderr, "gbm_bo_create(_with_modifiers) failed\n");
            return false;
        }
    }

    if (!r->gbm.bo) {
        fprintf(stderr, "gbm_bo_create_with_modifiers failed\n");
        return false;
    }

    r->gbm.fd = gbm_bo_get_fd(r->gbm.bo);
    if (r->gbm.fd < 0) {
        fprintf(stderr, "Failed to get gbm fd\n");
        return false;
    }

    r->gbm.modifier = gbm_bo_get_modifier(r->gbm.bo);
    r->gbm.stride   = gbm_bo_get_stride_for_plane(r->gbm.bo, 0);
    r->gbm.offset   = gbm_bo_get_offset(r->gbm.bo, 0);
    r->gbm.plane_size = (uint64_t)r->gbm.stride * (uint64_t)r->h;
    r->gbm.renderMinor = renderMinor;

    return true;
}

bool VkCtx::create_image(VkImageDrmFormatModifierExplicitCreateInfoEXT* drm, clientRes* r, VkImage& image,
                         VkImageUsageFlags usage, VkImageTiling tiling, VkExternalMemoryHandleTypeFlags handle) {
    VkExternalMemoryImageCreateInfo extImg{
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = handle,
    };

    if (drm)
        extImg.pNext = drm;

    VkImageCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = fmt,
        .extent = {r->w, r->h, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = tiling,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (handle > 0)
        ci.pNext = &extImg;

    VkResult ret = vkCreateImage(device, &ci, nullptr, &image);
    return ret == VK_SUCCESS;
}

bool VkCtx::allocate_memory(VkImage image, VkDeviceMemory& memory, clientRes* r,
                            VkDeviceSize* allocSize, bool external, bool import_dmabuf,
                            VkExternalMemoryHandleTypeFlags handleType) {
    VkImageMemoryRequirementsInfo2 info2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
        .image = image,
    };

    VkMemoryDedicatedRequirements dedicatedReq{
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
    };

    VkMemoryRequirements2 req2{
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = &dedicatedReq,
    };

    vkGetImageMemoryRequirements2(device, &info2, &req2);
    if (allocSize) *allocSize = req2.memoryRequirements.size;

    uint32_t memType = UINT32_MAX;

    int import_fd = -1;

    if (!external) {
        memType = find_mem_type(req2.memoryRequirements.memoryTypeBits, 0);
        if (memType == UINT32_MAX) {
            vkDestroyImage(device, image, nullptr);
            return false;
        }

        VkMemoryAllocateInfo ai{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = req2.memoryRequirements.size,
            .memoryTypeIndex = memType,
        };

        VkResult ret = vkAllocateMemory(device, &ai, nullptr, &memory);
        if (ret != VK_SUCCESS) {
            vkDestroyImage(device, image, nullptr);
            return false;
        }

        ret = vkBindImageMemory(device, image, memory, 0);
        if (ret != VK_SUCCESS) {
            vkFreeMemory(device, memory, nullptr);
            vkDestroyImage(device, image, nullptr);
            return false;
        }
        return true;
    }

    VkMemoryDedicatedAllocateInfo dedicatedAlloc{
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = image,
    };

    VkMemoryAllocateInfo ai{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req2.memoryRequirements.size,
    };

    VkExportMemoryAllocateInfo exportInfo{ VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
    VkImportMemoryFdInfoKHR importInfo{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR };

    if (import_dmabuf) {
        import_fd = dup(r->gbm.fd);
        if (import_fd < 0) {
            vkDestroyImage(device, image, nullptr);
            return false;
        }

        uint32_t bits = compatible_bits_for_dmabuf_import(image, import_fd);
        if (!bits) {
            close(import_fd);
            vkDestroyImage(device, image, nullptr);
            return false;
        }

        memType = find_mem_type(bits, handleType);
        if (memType == UINT32_MAX) {
            close(import_fd);
            vkDestroyImage(device, image, nullptr);
            return false;
        }

        importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        importInfo.fd = import_fd;
        if (dedicatedReq.requiresDedicatedAllocation || dedicatedReq.prefersDedicatedAllocation)
            importInfo.pNext = &dedicatedAlloc;

        ai.pNext = &importInfo;
        ai.memoryTypeIndex = memType;
    } else {
        memType = find_mem_type(req2.memoryRequirements.memoryTypeBits, handleType);
        if (memType == UINT32_MAX) {
            vkDestroyImage(device, image, nullptr);
            return false;
        }

        exportInfo.handleTypes = handleType;
        if (dedicatedReq.requiresDedicatedAllocation || dedicatedReq.prefersDedicatedAllocation)
            exportInfo.pNext = &dedicatedAlloc;

        ai.pNext = &exportInfo;
        ai.memoryTypeIndex = memType;
    }

    VkResult ret = vkAllocateMemory(device, &ai, nullptr, &memory);
    if (ret != VK_SUCCESS) {
        if (import_dmabuf) close(import_fd);
        vkDestroyImage(device, image, nullptr);
        return false;
    }

    ret = vkBindImageMemory(device, image, memory, 0);
    if (ret != VK_SUCCESS) {
        vkFreeMemory(device, memory, nullptr);
        vkDestroyImage(device, image, nullptr);
        return false;
    }
    return true;
}

bool VkCtx::create_view(VkImage image, VkDeviceMemory memory, VkImageView& view, VkFormat fmt) {
    VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    vi.subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

    VkResult ret = vkCreateImageView(device, &vi, nullptr, &view);
    if (ret != VK_SUCCESS) {
        vkFreeMemory(device, memory, nullptr);
        vkDestroyImage(device, image, nullptr);
        return false;
    }
    return ret == VK_SUCCESS;
}

void VkCtx::submit(std::shared_ptr<clientRes>& r) {
    std::lock_guard lock(m);
    imgui->draw(r);
    // if window size changed we need to recreate the images, bail.
    if (r->reinit_dmabuf)
        return;

    auto& cmd = imgui->cmd;

    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    if (in_flight_release_sem) {
        vkDestroySemaphore(device, in_flight_release_sem, nullptr);
        in_flight_release_sem = VK_NULL_HANDLE;
    }

    vkResetFences(device, 1, &fence);

    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    transition_image(cmd, r->src, r->src_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    r->src_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkClearValue clear{};
    VkRenderingAttachmentInfo colorAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAtt.imageView = r->src_view;
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
    ri.renderArea.extent = {r->w, r->h};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &colorAtt;

    vkCmdBeginRendering(cmd, &ri);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);

    transition_image(cmd, r->src, r->src_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    r->src_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    if (use_dmabuf)
        copy_to_dst(r->dmabuf, r->dmabuf_layout, VK_IMAGE_LAYOUT_GENERAL, r.get());

    copy_to_dst(r->opaque, r->opaque_layout, VK_IMAGE_LAYOUT_GENERAL, r.get());

    vkEndCommandBuffer(cmd);

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

    VkSemaphore releaseSem = VK_NULL_HANDLE;
    if (vkCreateSemaphore(device, &sci, nullptr, &releaseSem) != VK_SUCCESS) {
        return;
    }

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &releaseSem;

    vkQueueSubmit(graphicsQueue, 1, &si, fence);

    VkSemaphoreGetFdInfoKHR fdinfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .pNext = nullptr,
        .semaphore = releaseSem,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT_KHR,
    };

    int out_fd = -1;
    // This produces a sync fd that will trigger a false positive double close
    // in valgrind etc. Be warned so you don't spend 3 days like I did
    // tracking it down.
    VkResult vr = pfn_vkGetSemaphoreFdKHR(device, &fdinfo, &out_fd);
    if (vr != VK_SUCCESS) {
        return;
    }

    {
        std::lock_guard lock(r->m);
        if (r->acquire_fd >= 0) {
            close(r->acquire_fd);
            r->acquire_fd = -1;
        }

        r->acquire_fd = out_fd;
        out_fd = -1;
    }

    in_flight_release_sem = releaseSem;
}

void VkCtx::transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL &&
            newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {

        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        // Safe fallback: make writes visible, keep going.
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd,
                         srcStage, dstStage,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &b);
}

void VkCtx::copy_to_dst(VkImage dst, VkImageLayout& curLayout, VkImageLayout finalLayout, clientRes* r) {
    auto& cmd = imgui->cmd;
    transition_image(cmd, dst, curLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {r->w, r->h, 1};

    vkCmdCopyImage(cmd,
        r->src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst,  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region);

    transition_image(cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout);
    curLayout = finalLayout;
}

VkCtx::~VkCtx() {
    if (device) {
        vkDeviceWaitIdle(device);
        imgui.reset();
        frame_queue.clear();

        if (in_flight_release_sem) {
            vkDestroySemaphore(device, in_flight_release_sem, nullptr);
            in_flight_release_sem = VK_NULL_HANDLE;
        }
        if (fence) {
             vkDestroyFence(device, fence, nullptr);
             fence = VK_NULL_HANDLE;
        }

        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    if (debugMessenger != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
        auto pfnDestroyDebugUtilsMessengerEXT =
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (pfnDestroyDebugUtilsMessengerEXT) pfnDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }
    if (instance) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
    if (phys_fd_ >= 0) {
        close(phys_fd_);
        phys_fd_ = -1;
    }

    physicalDevice = VK_NULL_HANDLE;
    graphicsQueue = VK_NULL_HANDLE;
    graphicsQueueFamilyIndex = UINT32_MAX;
    pfn_vkGetMemoryFdPropertiesKHR = nullptr;
    pfn_vkGetSemaphoreFdKHR = nullptr;
}
