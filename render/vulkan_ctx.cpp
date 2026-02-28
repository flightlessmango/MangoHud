#include <stdexcept>
#include <string>
#include <fcntl.h>
#include "vulkan_ctx.h"
#include <gbm.h>
#include <drm/drm_fourcc.h>
#include "unistd.h"
#include "mesa/os_time.h"
#include "../server/common/helpers.hpp"

vkb::PhysicalDevice VkCtx::pick_device(vkb::Instance instance) {
    vkb::PhysicalDeviceSelector selector{instance};
    selector.set_minimum_version(1, 3);
    selector.require_present(false);

    VkPhysicalDeviceVulkan13Features f13{};
    f13.dynamicRendering = VK_TRUE;
    selector.set_required_features_13(f13);

    VkPhysicalDeviceVulkan12Features f12{};
    f12.timelineSemaphore = VK_TRUE;
    selector.set_required_features_12(f12);

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
    pfn_vkImportSemaphoreFdKHR = (PFN_vkImportSemaphoreFdKHR)vkGetDeviceProcAddr(device, "vkImportSemaphoreFdKHR");
    pfn_vkSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
}

int VkCtx::phys_fd() {
    if (phys_fd_)
        return phys_fd_.get();

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
    phys_fd_ = unique_fd::adopt(open(path.c_str(), O_RDWR | O_CLOEXEC));
    if (!phys_fd_) throw std::runtime_error("Failed to open " + path);
    return phys_fd_.get();
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

void VkCtx::init_client(clientRes* r, size_t buffer_size) {
    std::lock_guard lock(m);
    if (!r->w) r->w = 500;
    if (!r->h) r->h = 500;
    if (!device)
        return;

    r->device = device;
    r->server_render_minor = renderMinor;
    if (r->buffer.size() < buffer_size)
        r->buffer.resize(buffer_size);

    for (auto& buf : r->buffer) {
        if (!create_gbm_buffer(r, &buf.dmabuf))
            SPDLOG_ERROR("init gbm failed");
        if (!create_dmabuf(r, &buf.dmabuf))
            SPDLOG_ERROR("init dmabuf failed");
        if (!create_opaque(r, &buf.opaque))
            SPDLOG_ERROR("init opaque failed");
        if (!create_src(r, &buf.source))
            SPDLOG_ERROR("init source failed");


        create_sync(&buf);
        create_cmd(r, &buf.sync);
    }

    // TODO run imgui->draw once to calculate the initial width/height
    // this is currently a double lock so we need to redesign this a bit
    // we want to do this so we don't end up always pushing two dmabufs on connect
    // and when the the overlay changes
    // imgui->draw(r);

    r->send_dmabuf = true;
    r->initialized = true;
}

// TODO rename this to init sync or something
void VkCtx::create_sync(slot_t* s) {
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

    VkResult r = vkCreateSemaphore(device, &sci, nullptr, &s->sync.semaphore);
    if (r != VK_SUCCESS)
        SPDLOG_ERROR("vkCreateSemaphore {}", string_VkResult(r));


    VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device, &fci, nullptr, &s->sync.fence);
}

bool VkCtx::create_src(clientRes* r, source_t* source) {
    create_image(NULL, r, source->image_res.image,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_TILING_OPTIMAL, 0);
    allocate_memory(
        source->image_res.image,
        source->image_res.mem,
        r,
        nullptr,
        false,           // external
        false,           // import_dmabuf
        0,               // handleType (ignored)
        -1               // fd (only for dmabuf)
    );
    create_view(source->image_res.image, source->image_res.mem, source->image_res.view, fmt);
    return true;
}

bool VkCtx::create_dmabuf(clientRes* r, dmabuf_t* buf) {
    VkSubresourceLayout plane0{
        .offset = buf->gbm.offset,
        .size = 0,
        .rowPitch = buf->gbm.stride,
        .arrayPitch = 0,
        .depthPitch = 0,
    };

    VkImageDrmFormatModifierExplicitCreateInfoEXT drmExplicit{
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
        .drmFormatModifier = buf->gbm.modifier,
        .drmFormatModifierPlaneCount = 1,
        .pPlaneLayouts = &plane0,
    };

    if (!create_image(&drmExplicit, r, buf->image_res.image,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT))
        return false;

    if (!allocate_memory(
        buf->image_res.image,
        buf->image_res.mem,
        r,
        nullptr,         // allocSize
        true,            // external
        true,            // import_dmabuf
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        buf->gbm.fd
    ))
        return false;

    if (!create_view(buf->image_res.image, buf->image_res.mem, buf->image_res.view, fmt))
        return false;

    return true;
}

bool VkCtx::create_opaque(clientRes* r, opauqe_t* opaque) {
    create_image(NULL, r, opaque->image_res.image,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_IMAGE_TILING_OPTIMAL, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
    allocate_memory(
        opaque->image_res.image,
        opaque->image_res.mem,
        r,
        &opaque->size,
        true,            // external
        false,           // import_dmabuf
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
        -1               // fd (only for dmabuf)
    );
    create_view(opaque->image_res.image, opaque->image_res.mem, opaque->image_res.view, fmt);
    opaque->fd = unique_fd::adopt(export_opaquefd(opaque->image_res.mem));
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

bool VkCtx::create_gbm_buffer(clientRes* r, dmabuf_t* buf) {
    buf->gbm.fourcc = DRM_FORMAT_ARGB8888;
    if (!gbm_dev)
        gbm_dev = gbm_create_device(phys_fd());

    const uint64_t linear = DRM_FORMAT_MOD_LINEAR;
    buf->gbm.bo = gbm_bo_create_with_modifiers(gbm_dev, r->w, r->h, buf->gbm.fourcc, &linear, 1);
    if (!buf->gbm.bo) {
        buf->gbm.bo = gbm_bo_create(gbm_dev, r->w, r->h, buf->gbm.fourcc, GBM_BO_USE_RENDERING);
        if (!buf->gbm.bo) {
            fprintf(stderr, "gbm_bo_create_with_modifiers failed\n");
            throw;
            return false;
        }
    }

    buf->gbm.fd = unique_fd::adopt(gbm_bo_get_fd(buf->gbm.bo));
    if (!buf->gbm.fd) {
        fprintf(stderr, "Failed to get gbm fd\n");
        throw;
        return false;
    }

    buf->gbm.modifier = gbm_bo_get_modifier(buf->gbm.bo);
    buf->gbm.stride   = gbm_bo_get_stride_for_plane(buf->gbm.bo, 0);
    buf->gbm.offset   = gbm_bo_get_offset(buf->gbm.bo, 0);
    buf->gbm.plane_size = (uint64_t)buf->gbm.stride * (uint64_t)r->h;
    buf->gbm.renderMinor = renderMinor;

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
    if (ret != VK_SUCCESS) {
        SPDLOG_ERROR("vkCreateImage {}", string_VkResult(ret));
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool VkCtx::allocate_memory(VkImage image, VkDeviceMemory& memory, clientRes* r,
                            VkDeviceSize* allocSize, bool external, bool import_dmabuf,
                            VkExternalMemoryHandleTypeFlags handleType, int fd) {
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

    if (!external) {
        memType = find_mem_type(req2.memoryRequirements.memoryTypeBits, 0);
        if (memType == UINT32_MAX) {
            vkDestroyImage(device, image, nullptr);
            image = VK_NULL_HANDLE;
            throw;
        }

        VkMemoryAllocateInfo ai{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = req2.memoryRequirements.size,
            .memoryTypeIndex = memType,
        };

        VkResult ret = vkAllocateMemory(device, &ai, nullptr, &memory);
        if (ret != VK_SUCCESS) {
            SPDLOG_ERROR("vkAllocateMemory {}", string_VkResult(ret));
            vkDestroyImage(device, image, nullptr);
            image = VK_NULL_HANDLE;
            throw;
        }

        ret = vkBindImageMemory(device, image, memory, 0);
        if (ret != VK_SUCCESS) {
            SPDLOG_ERROR("vkBindImageMemory {}", string_VkResult(ret));
            vkFreeMemory(device, memory, nullptr);
            vkDestroyImage(device, image, nullptr);
            memory = VK_NULL_HANDLE;
            image = VK_NULL_HANDLE;
            throw;
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
    unique_fd import_fd;
    if (fd >= 0)
        import_fd = unique_fd::dup(fd);

    if (import_dmabuf) {
        if (!import_fd) {
            vkDestroyImage(device, image, nullptr);
            image = VK_NULL_HANDLE;
            return false;
        }

        uint32_t bits = compatible_bits_for_dmabuf_import(image, import_fd);
        if (!bits) {
            vkDestroyImage(device, image, nullptr);
            image = VK_NULL_HANDLE;
            return false;
        }

        memType = find_mem_type(bits, handleType);
        if (memType == UINT32_MAX) {
            vkDestroyImage(device, image, nullptr);
            image = VK_NULL_HANDLE;
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
            image = VK_NULL_HANDLE;
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
        SPDLOG_ERROR("vkAllocateMemory {}", string_VkResult(ret));
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
        throw std::runtime_error("vkAllocateMemory");
        return false;
    }

    ret = vkBindImageMemory(device, image, memory, 0);
    if (ret != VK_SUCCESS) {
        SPDLOG_ERROR("vkBindImageMemory {}", string_VkResult(ret));
        vkFreeMemory(device, memory, nullptr);
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        throw std::runtime_error("vkBindImageMemory");
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
        SPDLOG_ERROR("vkCreateImageView {}", string_VkResult(ret));
        vkFreeMemory(device, memory, nullptr);
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return false;
    }
    return ret == VK_SUCCESS;
}

bool VkCtx::submit(std::shared_ptr<clientRes>& r, int idx) {
    slot_t& buf = r->buffer[idx];

    transition_image(buf.sync.cmd, buf.source.image_res.image, buf.source.image_res.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    buf.source.image_res.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    if (use_dmabuf)
        copy_to_dst(buf.dmabuf.image_res.image, buf.dmabuf.image_res.layout, VK_IMAGE_LAYOUT_GENERAL, r.get(), buf);

    copy_to_dst(buf.opaque.image_res.image, buf.opaque.image_res.layout, VK_IMAGE_LAYOUT_GENERAL, r.get(), buf);

    vkEndCommandBuffer(buf.sync.cmd);

    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &buf.sync.cmd;

    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &buf.sync.semaphore;

    {
        std::scoped_lock lock(m);
        vkQueueSubmit(graphicsQueue, 1, &submit, buf.sync.fence);
    }

    return true;
}

int VkCtx::get_semaphore_fd(VkSemaphore sema) {
    VkSemaphoreGetFdInfoKHR fdinfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .pNext = nullptr,
        .semaphore = sema,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT_KHR,
    };

    int out_fd = -1;
    // This produces a sync fd that will trigger a false positive double close
    // in valgrind etc. Be warned so you don't spend 3 days like I did
    // tracking it down.
    VkResult r = pfn_vkGetSemaphoreFdKHR(device, &fdinfo, &out_fd);
    if (r != VK_SUCCESS)
        SPDLOG_ERROR("vkGetSemaphoreFdKHR {}", string_VkResult(r));


    return out_fd;
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
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
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

        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL &&
            newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
            newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else {
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

void VkCtx::copy_to_dst(VkImage dst, VkImageLayout& curLayout, VkImageLayout finalLayout, clientRes* r, slot_t& buf) {
    transition_image(buf.sync.cmd, dst, curLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {r->w, r->h, 1};

    vkCmdCopyImage(buf.sync.cmd,
        buf.source.image_res.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst,  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region);

    transition_image(buf.sync.cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout);
    curLayout = finalLayout;
}

void VkCtx::create_cmd(clientRes* r, sync_t* s) {
    if (!r->cmd_pool) {
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cp.queueFamilyIndex = graphicsQueueFamilyIndex;
        cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkResult ret = vkCreateCommandPool(device, &cp, nullptr, &r->cmd_pool);
        if (ret != VK_SUCCESS)
            SPDLOG_ERROR("vkCreateCommandPool failed {}", string_VkResult(ret));
    }

    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = r->cmd_pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VkResult ret = vkAllocateCommandBuffers(device, &ca, &s->cmd);
    if (ret != VK_SUCCESS)
        SPDLOG_ERROR("vkAllocateCommandBuffers failed {}", string_VkResult(ret));

    SetName(device, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)s->cmd, "buffer_cmd");
}

VkCtx::~VkCtx() {
    if (device) {
        vkDeviceWaitIdle(device);
        imgui.reset();
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

    if (gbm_dev)
        gbm_device_destroy(gbm_dev);

    physicalDevice = VK_NULL_HANDLE;
    graphicsQueue = VK_NULL_HANDLE;
    graphicsQueueFamilyIndex = UINT32_MAX;
    pfn_vkGetMemoryFdPropertiesKHR = nullptr;
    pfn_vkGetSemaphoreFdKHR = nullptr;
}

// void VkCtx::sync_wait(std::shared_ptr<Client> client) {
//     pthread_setname_np(pthread_self(), "mangohud_sync");
//     while (!client->stop_wait.load()) {
//         std::vector<VkFence> fences;
//         {
//             std::scoped_lock lock(client->resources->m, client->m);
//             for (auto [i, buf] : enumerate(client->resources->buffer)) {
//                 if (!contains(client->frame_queue, i))
//                     fences.push_back(buf.sync.fence);
//             }
//         }

//         if (fences.empty())
//             continue;

//         VkResult r = vkWaitForFences(device, fences.size(), fences.data(), VK_FALSE, 100'000'000);

//         if (r == VK_TIMEOUT) {
//             std::vector<VkSemaphore> semaphores;
//             for (auto& buf : client->resources->buffer)
//                 semaphores.push_back(buf.sync.semaphore);
//             VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
//             si.signalSemaphoreCount = semaphores.size();
//             si.pSignalSemaphores = semaphores.data();

//             std::lock_guard lock(m);
//             vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE);
//             continue;
//         }

//         for (auto [i, fence] : enumerate(fences)) {
//             if (vkGetFenceStatus(device, fence) == VK_SUCCESS) {
//                 std::lock_guard lock(client->m);
//                 client->frame_queue.push_back(i);
//                 vkResetFences(device, 1, &fence);
//                 SPDLOG_DEBUG("add frame {}", i);
//                 client->cv.notify_all();
//                 break;
//             }
//         }
//     }
//     SPDLOG_DEBUG("exited sync thread");
// }

// void VkCtx::wait_on_semaphores(std::shared_ptr<Client> client) {
//     std::vector<VkSemaphore> semaphores;
//     std::vector<uint64_t> values;

//     {
//         std::lock_guard lock(client->resources->m);
//         for (auto& buf : client->resources->buffer) {
//             semaphores.push_back(buf.sync.consumer_semaphore);
//             values.push_back(buf.sync.consumer_last);
//         }
//     }

//     VkSemaphoreWaitInfo info{};
//     info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
//     info.flags = VK_SEMAPHORE_WAIT_ANY_BIT;
//     info.semaphoreCount = static_cast<uint32_t>(semaphores.size());
//     info.pSemaphores = semaphores.data();
//     info.pValues = values.data();
//     while (!client->semaphores_stop.load()) {
//         VkResult r = vkWaitSemaphores(device, &info, 100'000'000);

//         if (r == VK_TIMEOUT)
//             continue;

//         for (auto [i, sem] : enumerate(semaphores)) {
//             uint64_t v;
//             vkGetSemaphoreCounterValue(device, sem, &v);
//             if (v >= values[i]) {
//                 values[i] = v + 1;
//                 {
//                     std::lock_guard lock(client->resources->m);
//                     client->resources->buffer[i].sync.consumer_last = v;
//                 }

//                 std::lock_guard lock(client->m);
//                 SPDLOG_DEBUG("queue slot {} counter {} pid {}", i, v, client->pid);
//                 client->frame_queue.push_back(i);
//                 client->cv.notify_one();
//                 continue;
//             }
//         }
//     }
// }
