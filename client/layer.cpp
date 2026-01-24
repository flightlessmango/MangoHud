#define VKROOTS_LAYER_IMPLEMENTATION
#include <memory>
#include <unordered_map>

#include "vkroots.h"
#include "mesa/os_time.h"
#include "vulkan.h"
#include "fps_limiter.h"
#include "spdlog_forward.h"
#include <spdlog/sinks/stdout_color_sinks.h>
static std::shared_ptr<spdlog::logger> logger;

std::string pEngineName;
uint32_t renderMinor = 0;
static auto& queue_family = *new std::unordered_map<VkQueue, uint32_t>();
static auto& q_family_mtx = *new std::mutex();
PFN_vkSetDeviceLoaderData g_set_device_loader_data = nullptr;
std::shared_ptr<OverlayVK> overlay_vk;
std::unique_ptr<fpsLimiter> fps_limiter;

class VkInstanceOverrides {
public:
    static VkResult CreateDevice(
        const vkroots::VkInstanceDispatch* dispatch,
        VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDevice* pDevice)
    {
        std::vector<const char*> exts;
        exts.reserve((pCreateInfo ? pCreateInfo->enabledExtensionCount : 0) + 8);

        if (pCreateInfo) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++)
            exts.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
        }

        auto add = [&](const char* n) {
            for (auto* e : exts) if (e && std::strcmp(e, n) == 0) return;
            exts.push_back(n);
        };

        add("VK_KHR_external_memory");
        add("VK_KHR_external_memory_fd");
        add("VK_EXT_external_memory_dma_buf");
        add("VK_KHR_external_semaphore");
        add("VK_KHR_external_semaphore_fd");
        add("VK_EXT_image_drm_format_modifier");
        add("VK_KHR_bind_memory2");
        add("VK_KHR_get_memory_requirements2");
        add("VK_KHR_sampler_ycbcr_conversion");
        add("VK_KHR_image_format_list");
        add("VK_KHR_maintenance1");

        VkDeviceCreateInfo ci = *pCreateInfo;
        ci.enabledExtensionCount = (uint32_t)exts.size();
        ci.ppEnabledExtensionNames = exts.data();

        const VkLayerDeviceCreateInfo* layer_info =
            (const VkLayerDeviceCreateInfo*)pCreateInfo->pNext;

        while (layer_info) {
            if (layer_info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
                layer_info->function == VK_LOADER_DATA_CALLBACK) {
                g_set_device_loader_data = layer_info->u.pfnSetDeviceLoaderData;
                break;
            }

            layer_info = (const VkLayerDeviceCreateInfo*)layer_info->pNext;
        }

        if (!g_set_device_loader_data) {
            fprintf(stderr, "failed to get device loader data\n");
            fprintf(stderr, "we will get validation errors\n");
        }
        return dispatch->CreateDevice(physicalDevice, &ci, pAllocator, pDevice);
    }

    static VkResult CreateInstance(
        PFN_vkCreateInstance            pfnCreateInstance,
        const VkInstanceCreateInfo*     pCreateInfo,
        const VkAllocationCallbacks*    pAllocator,
        VkInstance*                     pInstance)
    {
        if (!logger)
            logger = spdlog::stderr_color_mt("MANGOHUD");

        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug);
        const char* engine = "";
        if (pCreateInfo && pCreateInfo->pApplicationInfo && pCreateInfo->pApplicationInfo->pEngineName)
            engine = pCreateInfo->pApplicationInfo->pEngineName;

        pEngineName = engine;

        std::vector<const char*> exts;
        exts.reserve((pCreateInfo ? pCreateInfo->enabledExtensionCount : 0) + 8);

        if (pCreateInfo) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++)
            exts.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
        }

        auto add = [&](const char* n) {
            for (auto* e : exts) if (e && std::strcmp(e, n) == 0) return;
            exts.push_back(n);
        };

        add("VK_KHR_external_memory_capabilities");
        add("VK_KHR_external_semaphore_capabilities");
        add("VK_EXT_debug_utils");

        VkInstanceCreateInfo ci = {};
        if (pCreateInfo) {
            ci = *pCreateInfo;
        }
        ci.enabledExtensionCount = (uint32_t)exts.size();
        ci.ppEnabledExtensionNames = exts.data();

        return pfnCreateInstance(&ci, pAllocator, pInstance);
    }
};

class VkDeviceOverrides {
public:
    static VkResult CreateSwapchainKHR(
        const vkroots::VkDeviceDispatch* pDispatch,
        VkDevice device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSwapchainKHR* pSwapchain)
    {
        VkResult r = pDispatch->CreateSwapchainKHR(pDispatch->Device, pCreateInfo, pAllocator, pSwapchain);
        if (r != VK_SUCCESS)
            return r;

        auto sc = std::make_shared<swapchain_data>();
        sc->d = pDispatch;
        sc->format = pCreateInfo->imageFormat;
        sc->extent = pCreateInfo->imageExtent;
        sc->colorspace = pCreateInfo->imageColorSpace;

        uint32_t count = 0;
        r = pDispatch->GetSwapchainImagesKHR(pDispatch->Device, *pSwapchain, &count, nullptr);
        if (r != VK_SUCCESS || count == 0) {
            pDispatch->DestroySwapchainKHR(pDispatch->Device, *pSwapchain, pAllocator);
            return (r == VK_SUCCESS) ? VK_ERROR_INITIALIZATION_FAILED : r;
        }

        sc->images.resize(count);
        r = pDispatch->GetSwapchainImagesKHR(pDispatch->Device, *pSwapchain, &count, sc->images.data());
        if (r != VK_SUCCESS) {
            pDispatch->DestroySwapchainKHR(pDispatch->Device, *pSwapchain, pAllocator);
            return r;
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
                return r;
            }
        }

        VkAttachmentDescription ad{};
        ad.format = sc->format;
        ad.samples = VK_SAMPLE_COUNT_1_BIT;
        ad.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        ad.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ad.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        ad.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        ad.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ad.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

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
            return r;
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
                return r;
            }
        }

        if (!overlay_vk) overlay_vk = std::make_shared<OverlayVK>(g_set_device_loader_data);
        {
            std::lock_guard lock(overlay_vk->swapchain_mtx);
            overlay_vk->swapchains[*pSwapchain] = sc;
            overlay_vk->g_vkSetDebugUtilsObjectNameEXT =
            reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
                vkGetInstanceProcAddr(pDispatch->pPhysicalDeviceDispatch->Instance, "vkSetDebugUtilsObjectNameEXT"));
        }
        return VK_SUCCESS;
    }

    static void DestroySwapchainKHR(
    const vkroots::VkDeviceDispatch* pDispatch,
    VkDevice device,
    VkSwapchainKHR swapchain,
    const VkAllocationCallbacks* pAllocator)
    {
        auto it = overlay_vk->swapchains.find(swapchain);
        if (it != overlay_vk->swapchains.end())
            it->second.reset();
        pDispatch->DestroySwapchainKHR(pDispatch->Device, swapchain, pAllocator);
    }

    static VkResult QueuePresentKHR(
        const vkroots::VkDeviceDispatch* pDispatch,
        VkQueue queue,
        const VkPresentInfoKHR* pPresentInfo)
    {
        // static uint64_t present_counter = 0;
        // present_counter++;
        // if ((present_counter % 300) == 0) {
        //     uint64_t w = fps_limiter->q_limiter->waits.load();
        //     uint64_t ns = fps_limiter->q_limiter->waited_ns.load();
        //     uint64_t md = fps_limiter->q_limiter->max_depth_seen.load();
        //     SPDLOG_ERROR("queueLimiter: waits={}, waited_ms={:.3f}, max_depth={}, inflight={}",
        //                 w, (double)ns / 1e6, md, fps_limiter->q_limiter->in_flight.size());
        // }

        if (!renderMinor) {
            VkPhysicalDeviceDrmPropertiesEXT drm_props{};
            drm_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;
            drm_props.pNext = nullptr;

            VkPhysicalDeviceProperties2KHR props2{};
            props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext = &drm_props;

            auto fpGetPhysicalDeviceProperties2KHR =
                reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2KHR>(
                    pDispatch->pPhysicalDeviceDispatch->pInstanceDispatch->GetInstanceProcAddr(
                    pDispatch->pPhysicalDeviceDispatch->Instance, "vkGetPhysicalDeviceProperties2KHR"));

            fpGetPhysicalDeviceProperties2KHR(pDispatch->PhysicalDevice, &props2);
            if (drm_props.hasPrimary)
                renderMinor = drm_props.renderMinor;

            SPDLOG_DEBUG("renderMinor: {}", renderMinor);
        }

        if (!fps_limiter)
            fps_limiter = std::make_unique<fpsLimiter>(false);

        {
            std::lock_guard lock(fps_limiter->q_limiter->present_queues_mtx);
            fps_limiter->q_limiter->present_queues.insert(queue);
        }

        fps_limiter->limit(true);

        if (!overlay_vk) overlay_vk = std::make_shared<OverlayVK>(g_set_device_loader_data);
        if (!overlay_vk->ipc) overlay_vk->ipc = std::make_unique<IPCClient>(renderMinor, pEngineName);
        overlay_vk->ipc->add_to_queue(os_time_get_nano());
        // TODO Probably don't do this every frame
        fps_limiter->set_fps_limit(overlay_vk->ipc->fps_limit);

        uint32_t family = 0;
        {
            std::lock_guard lock(q_family_mtx);
            auto it = queue_family.find(queue);
            if (it != queue_family.end())
                family = it->second;
        }

        uint32_t swapchain_image_count = 0;
        pDispatch->GetSwapchainImagesKHR(pDispatch->Device, pPresentInfo->pSwapchains[0], &swapchain_image_count, nullptr);
        uint32_t imageIndex = pPresentInfo->pImageIndices[0];
        VkPresentInfoKHR pi = *pPresentInfo;
        {
            std::lock_guard lock(overlay_vk->m);
            if (!overlay_vk->draw(pDispatch, pPresentInfo->pSwapchains[0],
                family, swapchain_image_count, imageIndex, &pi, queue))
                return pDispatch->QueuePresentKHR(queue, pPresentInfo);
        }

        VkResult r = pDispatch->QueuePresentKHR(queue, &pi);
        fps_limiter->limit(false);
        return r;
    }

    static void GetDeviceQueue(
        const vkroots::VkDeviceDispatch* pDispatch,
        VkDevice device,
        uint32_t queueFamilyIndex,
        uint32_t queueIndex,
        VkQueue* pQueue)
    {
        pDispatch->GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
        std::lock_guard lock(q_family_mtx);
        queue_family.try_emplace(*pQueue, queueFamilyIndex);
    }

    static void GetDeviceQueue2(const vkroots::VkDeviceDispatch* pDispatch,
            VkDevice device,
            const VkDeviceQueueInfo2* pQueueInfo,
            VkQueue* pQueue)
    {
        pDispatch->GetDeviceQueue2(device, pQueueInfo, pQueue);
        std::lock_guard lock(q_family_mtx);
        queue_family.try_emplace(*pQueue, pQueueInfo->queueFamilyIndex);
    }

    static void DestroyDevice(const vkroots::VkDeviceDispatch* d, VkDevice device, const VkAllocationCallbacks* pAllocator) {
        d->DeviceWaitIdle(device);
        if (overlay_vk) overlay_vk.reset();

        d->DestroyDevice(device, pAllocator);
    }

    static VkResult QueueSubmit(const vkroots::VkDeviceDispatch* d, VkQueue queue, uint32_t submitCount,
                                const VkSubmitInfo *pSubmits, VkFence fence)
    {
        if (!fps_limiter)
            fps_limiter = std::make_unique<fpsLimiter>(false);

        auto& q_limiter = fps_limiter->q_limiter;
        if (q_limiter->is_present_queue(queue))
            q_limiter->throttle_before_submit(d);

        VkResult r = d->QueueSubmit(queue, submitCount, pSubmits, fence);
        if (r != VK_SUCCESS)
            return r;

        if (q_limiter->is_present_queue(queue)) {
            VkResult r2 = q_limiter->mark_after_submit(d, queue);
            if (r2 != VK_SUCCESS)
                return r2;
        }

        return VK_SUCCESS;
    }
};

VKROOTS_DEFINE_LAYER_INTERFACES(
  VkInstanceOverrides,
  vkroots::NoOverrides,
  VkDeviceOverrides
);
