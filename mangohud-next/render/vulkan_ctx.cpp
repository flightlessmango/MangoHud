#include <stdexcept>
#include <string>
#include <fcntl.h>
#include "vulkan_ctx.h"
#include <gbm.h>
#include <drm/drm_fourcc.h>
#include "unistd.h"
#include "mesa/os_time.h"
#include "../server/common/helpers.hpp"
#include "vulkan/vk_enum_string_helper.h"
#include "export.h"
#include "imgui/imgui_ctx.h"
#include "imgui/vk.h"

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

    selector.add_required_extension(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    selector.add_required_extension(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);

    auto devicesRet = selector.select_devices();
    if (!devicesRet) {
        SPDLOG_ERROR("Selector failed\n");
        return {};
    }

    for (const auto& dev : devicesRet.value()) {
        VkPhysicalDeviceDrmPropertiesEXT drm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        props2.pNext = &drm;
        vkGetPhysicalDeviceProperties2(dev.physical_device, &props2);

        if (!drm.hasRender)
            continue;

        if (renderer >= 0 &&
            static_cast<int64_t>(drm.renderMinor) != renderer)
            continue;

        renderer = drm.renderMinor;
        return dev;
    }

    if (renderer >= 0)
        SPDLOG_DEBUG("No Vulkan device matching renderD{}", renderer);
    else
        SPDLOG_ERROR("No Vulkan device with render node + dmabuf/modifier support");
    return {};
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
    if (!vkb_device.physical_device && renderer >= 0) {
        renderer = -1;
        vkb_device = pick_device(vkbInstance_);
    }

    if (!vkb_device.physical_device) {
        SPDLOG_ERROR("can't find GPU, bailing");
        std::abort();
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
    pfn_vkGetFenceFdKHR = (PFN_vkGetFenceFdKHR)vkGetDeviceProcAddr(device, "vkGetFenceFdKHR");
    pfn_vkImportSemaphoreFdKHR = (PFN_vkImportSemaphoreFdKHR)vkGetDeviceProcAddr(device, "vkImportSemaphoreFdKHR");
    pfn_vkSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
}

VkCtx::VkCtx(int64_t renderer_) : renderer(renderer_), imgui(std::make_shared<ImGuiCtx>()) {
    init(true);
};

void VkCtx::init_imgui()
{
    std::call_once(imgui_once, [&] {
        imgui->init_vk(this);
    });
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

bool VkCtx::init_client(std::vector<BufferSet>& buffers, VkCommandPool& cmd_pool,
                        uint32_t w, uint32_t h, size_t buffer_size, bool use_opaque) {
    std::lock_guard lock(m);
    if (!w) w = 500;
    if (!h) h = 500;
    if (!device)
        return false;

    if (buffers.size() < buffer_size)
        buffers.resize(buffer_size);

    for (auto& buf : buffers) {
        if (!create_gbm(w, h, &buf.dmabuf, phys_fd(), DRM_FORMAT_MOD_LINEAR)) {
            SPDLOG_ERROR("init gbm failed");
            return false;
        }

        if (!create_dmabuf(w, h, &buf.dmabuf)) {
            SPDLOG_ERROR("init dmabuf failed");
            return false;
        }

        if (use_opaque)
            if (!create_opaque(w, h, &buf.opaque)) {
                SPDLOG_ERROR("init opaque failed");
                return false;
            }

        if (!create_src(w, h, &buf.source)) {
            SPDLOG_ERROR("init source failed");
            return false;
        }

        if (!create_sync(&buf))
            return false;

        if (!create_cmd(cmd_pool, &buf.sync))
            return false;
    }

    // TODO run imgui->draw once to calculate the initial width/height
    // this is currently a double lock so we need to redesign this a bit
    // we want to do this so we don't end up always pushing two dmabufs on connect
    // and when the the overlay changes
    // imgui->draw(...);

    return true;
}

// TODO rename this to init sync or something
bool VkCtx::create_sync(BufferSet* s) {
    VkExportFenceCreateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO;
    export_info.handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.pNext = &export_info;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkResult ret = vkCreateFence(device, &fci, nullptr, &s->sync.fence);
    if (ret != VK_SUCCESS) {
        SPDLOG_ERROR("vkCreateFence failed {}", string_VkResult(ret));
        s->sync.fence = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool VkCtx::create_src(uint32_t w, uint32_t h, source_t* source) {
    if (!create_image(NULL, w, h, source->image_res.image,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                      VK_IMAGE_TILING_OPTIMAL, 0))
        return false;

    if (!allocate_memory(
        source->image_res.image,
        source->image_res.mem,
        nullptr,
        false,           // external
        false,           // import_dmabuf
        0,               // handleType (ignored)
        -1               // fd (only for dmabuf)
    ))
        return false;

    if (!create_view(source->image_res.image, source->image_res.mem, source->image_res.view, fmt))
        return false;

    return true;
}

bool VkCtx::create_dmabuf(uint32_t w, uint32_t h, dmabuf_t* buf) {
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

    if (!create_image(&drmExplicit, w, h, buf->image_res.image,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT))
        return false;

    if (!allocate_memory(
        buf->image_res.image,
        buf->image_res.mem,
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

bool VkCtx::create_opaque(uint32_t w, uint32_t h, opauqe_t* opaque) {
    if (!create_image(NULL, w, h, opaque->image_res.image,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                      VK_IMAGE_TILING_OPTIMAL, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT))
        return false;

    if (!allocate_memory(
        opaque->image_res.image,
        opaque->image_res.mem,
        &opaque->size,
        true,            // external
        false,           // import_dmabuf
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
        -1               // fd (only for dmabuf)
    ))
        return false;

    if (!create_view(opaque->image_res.image, opaque->image_res.mem, opaque->image_res.view, fmt))
        return false;

    opaque->fd = unique_fd::adopt(export_opaquefd(opaque->image_res.mem));
    if (!opaque->fd)
        return false;

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

bool VkCtx::create_image(VkImageDrmFormatModifierExplicitCreateInfoEXT* drm, uint32_t w, uint32_t h, VkImage& image,
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
        .extent = {w, h, 1},
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

bool VkCtx::allocate_memory(VkImage image, VkDeviceMemory& memory,
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
    int import_fd = -1;
    if (fd >= 0)
        import_fd = dup(fd);

    if (import_dmabuf) {
        if (import_fd < 0) {
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

        memType = find_mem_type(bits, 0);
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
        memType = find_mem_type(req2.memoryRequirements.memoryTypeBits,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
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

bool VkCtx::submit(std::vector<BufferSet>& buffers, uint32_t w, uint32_t h, Resolution& size,
                   int idx, bool use_opaque, std::shared_ptr<HudConfig> hud, std::mutex& hud_m) {
    BufferSet& buf = buffers[idx];

    if (!imgui->draw(w, h, size, &buf, Backend::VULKAN, hud, hud_m))
        return false;

    transition_image(buf.sync.cmd, buf.source.image_res.image, buf.source.image_res.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    buf.source.image_res.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    if (use_opaque)
        copy_to_dst(buf.opaque.image_res.image, buf.opaque.image_res.layout, VK_IMAGE_LAYOUT_GENERAL, w, h, buf);
    else
        copy_to_dst(buf.dmabuf.image_res.image, buf.dmabuf.image_res.layout, VK_IMAGE_LAYOUT_GENERAL, w, h, buf);

    VkResult ret = vkEndCommandBuffer(buf.sync.cmd);
    if (ret != VK_SUCCESS) {
        SPDLOG_ERROR("vkEndCommandBuffer failed {}", string_VkResult(ret));
        return false;
    }

    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &buf.sync.cmd;

    {
        std::scoped_lock lock(m);
        ret = vkQueueSubmit(graphicsQueue, 1, &submit, buf.sync.fence);
        if (ret != VK_SUCCESS) {
            SPDLOG_ERROR("vkQueueSubmit failed {}", string_VkResult(ret));
            return false;
        }
    }

    return true;
}

// int VkCtx::get_semaphore_fd(VkSemaphore sema) {
//     VkSemaphoreGetFdInfoKHR fdinfo{
//         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
//         .pNext = nullptr,
//         .semaphore = sema,
//         .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT_KHR,
//     };

//     int out_fd = -1;
//     // This produces a sync fd that will trigger a false positive double close
//     // in valgrind etc. Be warned so you don't spend 3 days like I did
//     // tracking it down.
//     VkResult r = pfn_vkGetSemaphoreFdKHR(device, &fdinfo, &out_fd);
//     if (r != VK_SUCCESS)
//         SPDLOG_ERROR("vkGetSemaphoreFdKHR {}", string_VkResult(r));


//     return out_fd;
// }

int VkCtx::get_fence_fd(VkFence fence) {
    VkFenceGetFdInfoKHR get_fd{};
    get_fd.sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR;
    get_fd.fence = fence;
    get_fd.handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
    int fd = -1;

    // This produces a sync fd that will trigger a false positive double close
    // in valgrind etc. Be warned so you don't spend 3 days like I did
    // tracking it down.
    VkResult r = pfn_vkGetFenceFdKHR(device, &get_fd, &fd);
    if (r != VK_SUCCESS)
        SPDLOG_ERROR("vkGetFenceFdKHR {}", string_VkResult(r));

    return fd;
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

void VkCtx::copy_to_dst(VkImage dst, VkImageLayout& curLayout, VkImageLayout finalLayout,
                        uint32_t w, uint32_t h, BufferSet& buf) {
    transition_image(buf.sync.cmd, dst, curLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {w, h, 1};

    vkCmdCopyImage(buf.sync.cmd,
        buf.source.image_res.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst,  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region);

    transition_image(buf.sync.cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout);
    curLayout = finalLayout;
}

bool VkCtx::create_cmd(VkCommandPool& cmd_pool, sync_t* s) {
    if (!cmd_pool) {
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cp.queueFamilyIndex = graphicsQueueFamilyIndex;
        cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkResult ret = vkCreateCommandPool(device, &cp, nullptr, &cmd_pool);
        if (ret != VK_SUCCESS) {
            SPDLOG_ERROR("vkCreateCommandPool failed {}", string_VkResult(ret));
            return false;
        }
    }

    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = cmd_pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VkResult ret = vkAllocateCommandBuffers(device, &ca, &s->cmd);
    if (ret != VK_SUCCESS) {
        SPDLOG_ERROR("vkAllocateCommandBuffers failed {}", string_VkResult(ret));
        s->cmd = VK_NULL_HANDLE;
        return false;
    }

    SetName(device, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)s->cmd, "buffer_cmd");
    return true;
}

VkCtx::~VkCtx() {
    imgui.reset();

    if (device) {
        vkDeviceWaitIdle(device);
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

    physicalDevice = VK_NULL_HANDLE;
    graphicsQueue = VK_NULL_HANDLE;
    graphicsQueueFamilyIndex = UINT32_MAX;
    pfn_vkGetMemoryFdPropertiesKHR = nullptr;
    pfn_vkGetSemaphoreFdKHR = nullptr;
}
