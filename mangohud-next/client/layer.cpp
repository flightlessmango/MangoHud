#define VKROOTS_LAYER_IMPLEMENTATION
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <array>

#include "vkroots.h"
#include "mesa/os_time.h"
#include "layer.h"
#include "file_utils.h"
#include "wayland.h"
#include "x11.h"


static char pendingEngineName[VK_MAX_DESCRIPTION_SIZE]{};
std::unique_ptr<Layer> layer;
static std::unique_ptr<Wayland> wayland;
static std::unique_ptr<X11> x11;

static const uint32_t overlay_vert_spv[] = {
    #include "overlay.vert.spv.h"
};
static const uint32_t overlay_frag_spv[] = {
    #include "overlay.frag.spv.h"
};

static bool ChainHasSType(const void* head, VkStructureType sType) {
    for (auto* it = (const VkBaseInStructure*)head; it; it = it->pNext) {
        if (it->sType == sType) {
            return true;
        }
    }
    return false;
}

static PFN_vkSetDeviceLoaderData FindSetDeviceLoaderData(const VkDeviceCreateInfo* pCreateInfo)
{
    if (!pCreateInfo)
        return nullptr;

    for (auto* it = reinterpret_cast<const VkBaseInStructure*>(pCreateInfo->pNext); it; it = it->pNext) {
        if (it->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO)
            continue;

        auto* layer_info = reinterpret_cast<const VkLayerDeviceCreateInfo*>(it);
        if (layer_info->function == VK_LOADER_DATA_CALLBACK)
            return layer_info->u.pfnSetDeviceLoaderData;
    }

    return nullptr;
}

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
        add("VK_KHR_present_id");
        add("VK_KHR_present_wait");

        VkDeviceCreateInfo ci = *pCreateInfo;
        ci.enabledExtensionCount = (uint32_t)exts.size();
        ci.ppEnabledExtensionNames = exts.data();

        Layer::set_device_loader_data.store(FindSetDeviceLoaderData(pCreateInfo), std::memory_order_release);

        if (!Layer::set_device_loader_data.load(std::memory_order_acquire))
            SPDLOG_ERROR("Failed to get vkSetDeviceLoaderData callback; layer-created dispatchable objects will not be tagged");

        VkPhysicalDevicePresentIdFeaturesKHR pid{};
        pid.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
        pid.presentId = VK_TRUE;

        VkPhysicalDevicePresentWaitFeaturesKHR pw{};
        pw.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
        pw.presentWait = VK_TRUE;

        void* newPNext = (void*)ci.pNext;

        if (!ChainHasSType(newPNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR)) {
            pw.pNext = newPNext;
            newPNext = &pw;
        }

        if (!ChainHasSType(newPNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR)) {
            pid.pNext = newPNext;
            newPNext = &pid;
        }

#ifdef VK_EXT_present_timing
        return PresentationTiming::create_device(dispatch, physicalDevice, ci, pAllocator, pDevice, exts, newPNext);
#else
        ci.pNext = newPNext;
        return dispatch->CreateDevice(physicalDevice, &ci, pAllocator, pDevice);
#endif
    }

    static VkResult CreateInstance(
        PFN_vkCreateInstance            pfnCreateInstance,
        const VkInstanceCreateInfo*     pCreateInfo,
        const VkAllocationCallbacks*    pAllocator,
        VkInstance*                     pInstance)
    {
        const char* engine = "";
        if (pCreateInfo && pCreateInfo->pApplicationInfo && pCreateInfo->pApplicationInfo->pEngineName)
            engine = pCreateInfo->pApplicationInfo->pEngineName;

        std::snprintf(pendingEngineName, sizeof(pendingEngineName), "%s", engine);

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
        add("VK_KHR_get_surface_capabilities2");
        add("VK_KHR_get_physical_device_properties2");

        VkInstanceCreateInfo ci = {};
        if (pCreateInfo) {
            ci = *pCreateInfo;
        }
        ci.enabledExtensionCount = (uint32_t)exts.size();
        ci.ppEnabledExtensionNames = exts.data();

#ifdef VK_EXT_present_timing
        return PresentationTiming::create_instance(pfnCreateInstance, ci, pAllocator, pInstance);
#else
        return pfnCreateInstance(&ci, pAllocator, pInstance);
#endif
    }

    static void DestroyInstance(const vkroots::VkInstanceDispatch* dispatch, VkInstance instance,
                                const VkAllocationCallbacks* allocator) {
#ifdef VK_EXT_present_timing
        PresentationTiming::destroy_instance(instance);
#endif
        dispatch->DestroyInstance(instance, allocator);
    }

    static VkResult CreateWaylandSurfaceKHR(const vkroots::VkInstanceDispatch* dispatch, VkInstance instance,
                                     const VkWaylandSurfaceCreateInfoKHR *pCreateInfo,
                                     const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
    {
        VkResult r = dispatch->CreateWaylandSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
        if (r != VK_SUCCESS)
            return r;
        if (!layer) layer = std::make_unique<Layer>();
        if (!wayland) wayland = std::make_unique<Wayland>(layer->ipc);
        wayland->add_surface(*pSurface, pCreateInfo->surface, pCreateInfo->display);
        return r;
    }

#ifdef VK_USE_PLATFORM_XLIB_KHR
    static VkResult CreateXlibSurfaceKHR(const vkroots::VkInstanceDispatch* dispatch, VkInstance instance,
                                        const VkXlibSurfaceCreateInfoKHR* info,
                                        const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface)
    {
        auto result = dispatch->CreateXlibSurfaceKHR(instance, info, allocator, surface);
        if (result == VK_SUCCESS) {
            if (!layer) layer = std::make_unique<Layer>();
            if (!x11) x11 = std::make_unique<X11>(layer->ipc);
            x11->set_window(info->window, DisplayString(info->dpy));
        }
        return result;
    }
#endif

    static VkResult CreateXcbSurfaceKHR(const vkroots::VkInstanceDispatch* dispatch, VkInstance instance,
                                       const VkXcbSurfaceCreateInfoKHR* info,
                                       const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface)
    {
        auto result = dispatch->CreateXcbSurfaceKHR(instance, info, allocator, surface);
        if (result == VK_SUCCESS) {
            if (!layer) layer = std::make_unique<Layer>();
            if (!x11) x11 = std::make_unique<X11>(layer->ipc);
            // TODO: we assume the application's XCB connection uses $DISPLAY. If it connects
            // to another X server, our focus listener watches the window on the wrong server.
            x11->set_window(info->window);
        }
        return result;
    }

    static void DestroySurfaceKHR(const vkroots::VkInstanceDispatch* dispatch, VkInstance instance,
                                  VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator)
    {
        if (wayland) wayland->destroy_surface(surface);
        return dispatch->DestroySurfaceKHR(instance, surface, pAllocator);
    }
};

class VkDeviceOverrides {
public:
#ifdef VK_EXT_present_timing
    static VkResult SetSwapchainPresentTimingQueueSizeEXT(
        const vkroots::VkDeviceDispatch* dispatch, VkDevice device,
        VkSwapchainKHR swapchain, uint32_t size) {
        auto set_queue = reinterpret_cast<PFN_vkSetSwapchainPresentTimingQueueSizeEXT>(
            dispatch->GetDeviceProcAddr(device, "vkSetSwapchainPresentTimingQueueSizeEXT"));
        auto sc = layer ? layer->get_swapchain_data(swapchain) : nullptr;
        if (!sc || !sc->timing.managed)
            return set_queue(device, swapchain, size);
        std::lock_guard lock(sc->timing.m);
        return sc->timing.set_application_queue_size(size);
    }

    static VkResult GetPastPresentationTimingEXT(
        const vkroots::VkDeviceDispatch* dispatch, VkDevice device,
        const VkPastPresentationTimingInfoEXT* info,
        VkPastPresentationTimingPropertiesEXT* properties) {
        auto sc = layer ? layer->get_swapchain_data(info->swapchain) : nullptr;
        if (!sc || !sc->timing.managed)
            return reinterpret_cast<PFN_vkGetPastPresentationTimingEXT>(
                dispatch->GetDeviceProcAddr(device, "vkGetPastPresentationTimingEXT"))(device, info, properties);
        std::lock_guard lock(sc->timing.m);
        return sc->timing.get_application_results(properties);
    }
#endif
    static VkResult CreateSwapchainKHR(
        const vkroots::VkDeviceDispatch* pDispatch,
        VkDevice device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSwapchainKHR* pSwapchain)
    {
        VkSwapchainCreateInfoKHR create_info = *pCreateInfo;
#ifdef VK_EXT_present_timing
        auto timing_config = PresentationTiming::prepare_swapchain(pDispatch, create_info);
#endif
        VkResult r = pDispatch->CreateSwapchainKHR(pDispatch->Device, &create_info, pAllocator, pSwapchain);
#ifdef VK_EXT_present_timing
        if (r != VK_SUCCESS && timing_config.enabled && create_info.flags != pCreateInfo->flags) {
            timing_config.enabled = false;
            r = pDispatch->CreateSwapchainKHR(pDispatch->Device, pCreateInfo, pAllocator, pSwapchain);
        }
#endif
        if (r != VK_SUCCESS)
            return r;

        uint32_t count = 0;
        r = pDispatch->GetSwapchainImagesKHR(pDispatch->Device, *pSwapchain, &count, nullptr);
        if (r != VK_SUCCESS || count == 0) {
            pDispatch->DestroySwapchainKHR(pDispatch->Device, *pSwapchain, pAllocator);
            return (r == VK_SUCCESS) ? VK_ERROR_INITIALIZATION_FAILED : r;
        }

        if (!layer) layer = std::make_unique<Layer>();
        layer->ipc->pEngineName = pendingEngineName;

        if (layer->ipc->renderMinor < 0 || layer->ipc->vulkanDriver.empty()) {
            VkPhysicalDeviceDrmPropertiesEXT drm_props{};
            drm_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;

            VkPhysicalDeviceDriverProperties driver_props{};
            driver_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
            drm_props.pNext = &driver_props;

            VkPhysicalDeviceProperties2KHR props2{};
            props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext = &drm_props;

            auto fpGetPhysicalDeviceProperties2KHR =
                reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2KHR>(
                    pDispatch->pPhysicalDeviceDispatch->pInstanceDispatch->GetInstanceProcAddr(
                    pDispatch->pPhysicalDeviceDispatch->Instance, "vkGetPhysicalDeviceProperties2KHR"));

            fpGetPhysicalDeviceProperties2KHR(pDispatch->PhysicalDevice, &props2);
            if (drm_props.hasRender)
                layer->ipc->renderMinor = drm_props.renderMinor;
            if (driver_props.driverInfo[0] != '\0')
                layer->ipc->vulkanDriver = driver_props.driverInfo;
            if (props2.properties.deviceName[0] != '\0')
                layer->ipc->gpuName = clean_gpu_name(props2.properties.deviceName);
        }

        layer->init_overlay_resources(pCreateInfo, pDispatch, count);
        r = layer->create_swapchain_data(pSwapchain, pCreateInfo, pDispatch);
        if (r != VK_SUCCESS)
            return r;
#ifdef VK_EXT_present_timing
        auto sc = layer->get_swapchain_data(*pSwapchain);
        layer->timing.init_swapchain(pDispatch, device, *pSwapchain, sc, timing_config);
#endif
        layer->ipc->send_resolution(pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height);

        layer->g_vkSetDebugUtilsObjectNameEXT =
        reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(pDispatch->pPhysicalDeviceDispatch->Instance, "vkSetDebugUtilsObjectNameEXT"));
        if (wayland)
            wayland->ensure_overlay(pCreateInfo->surface);
        return VK_SUCCESS;
    }

    static void DestroySwapchainKHR(
    const vkroots::VkDeviceDispatch* pDispatch,
    VkDevice device,
    VkSwapchainKHR swapchain,
    const VkAllocationCallbacks* pAllocator)
    {
#ifdef VK_EXT_present_timing
        if (layer)
            layer->timing.retire(swapchain);
#endif
        {
            std::unique_lock lock(layer->swapchain_mtx);
            auto it = layer->swapchains.find(swapchain);
            if (it != layer->swapchains.end()) {
                if (layer->cached_swapchain.handle == swapchain)
                    layer->cached_swapchain = {};
                layer->swapchains.erase(it);
            }
        }

        pDispatch->DestroySwapchainKHR(pDispatch->Device, swapchain, pAllocator);
    }

    static VkResult QueuePresentKHR(
        const vkroots::VkDeviceDispatch* pDispatch,
        VkQueue queue,
        const VkPresentInfoKHR* pPresentInfo)
    {
        auto sc = layer && pPresentInfo->swapchainCount > 0
            ? layer->get_swapchain_data(pPresentInfo->pSwapchains[0]) : nullptr;
        bool feedback = false;
#ifdef VK_EXT_present_timing
        feedback = PresentationTiming::check_present(sc, pPresentInfo);
#endif
        bool timing_fps = false;
#ifdef VK_EXT_present_timing
        timing_fps = feedback && sc->lsfg_is_present &&
            !has_type(pPresentInfo->pNext, VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT) &&
            (sc->timing.stage & VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT) &&
            sc->timing.pending.load(std::memory_order_relaxed) +
                sc->timing.presents_per_frame.load(std::memory_order_relaxed) <=
                sc->timing.driver_queue_size.load(std::memory_order_relaxed);
#endif
        if (layer && !timing_fps)
            layer->ipc->add_to_queue(os_time_get_nano());
        auto present = [&](const VkPresentInfoKHR* info) {
#ifdef VK_EXT_present_timing
            if (layer)
                return layer->timing.present(pDispatch, queue, info, sc, feedback, timing_fps);
#endif
            return pDispatch->QueuePresentKHR(queue, info);
        };
        if (!sc)
            return present(pPresentInfo);
        if (wayland) {
            return present(pPresentInfo);
        }

        if (!layer->overlay_vk) layer->overlay_vk = std::make_shared<OverlayVK>(layer.get());

        if (!layer->init_cmd(queue))
            return present(pPresentInfo);

        uint32_t swapchain_image_count = 0;
        pDispatch->GetSwapchainImagesKHR(pDispatch->Device, pPresentInfo->pSwapchains[0], &swapchain_image_count, nullptr);
        uint32_t imageIndex = pPresentInfo->pImageIndices[0];
        VkPresentInfoKHR pi = *pPresentInfo;

        if (pPresentInfo->swapchainCount > 1) {
            SPDLOG_DEBUG("QueuePresentKHR has {} swapchains; drawing overlay on index 0 and forwarding all swapchains",
                         pPresentInfo->swapchainCount);
            std::shared_lock lock(layer->swapchain_mtx);
            for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
                auto it = layer->swapchains.find(pPresentInfo->pSwapchains[i]);
                if (it != layer->swapchains.end()) {
                    SPDLOG_DEBUG("QueuePresentKHR swapchain[{}]=0x{:x} image={} size={}x{}",
                                 i, (uint64_t)pPresentInfo->pSwapchains[i], pPresentInfo->pImageIndices[i],
                                 it->second->extent.width, it->second->extent.height);
                } else {
                    SPDLOG_DEBUG("QueuePresentKHR swapchain[{}]=0x{:x} image={} size=unknown",
                                 i, (uint64_t)pPresentInfo->pSwapchains[i], pPresentInfo->pImageIndices[i]);
                }
            }
        }

        layer->ipc->start(swapchain_image_count);
        bool drew;
        {
            std::lock_guard lock(layer->overlay_vk->m);
            drew = layer->overlay_vk->draw(pPresentInfo->pSwapchains[0], imageIndex, queue, pi);
        }
        if (!drew)
            return present(pPresentInfo);

        VkSemaphore signal;
        {
            std::lock_guard lock(layer->overlay_vk->m);
            signal = layer->ovl_res->overlay_done[imageIndex];
        }

        VkPresentInfoKHR pi2 = *pPresentInfo;
        pi2.waitSemaphoreCount = 1;
        pi2.pWaitSemaphores = &signal;

        return present(&pi2);
    }

    static void GetDeviceQueue(
        const vkroots::VkDeviceDispatch* pDispatch,
        VkDevice device,
        uint32_t queueFamilyIndex,
        uint32_t queueIndex,
        VkQueue* pQueue)
    {
        pDispatch->GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
        if (!layer) layer = std::make_unique<Layer>();
        {
            std::lock_guard lock(layer->q_family_mtx);
            layer->queue_family.try_emplace(*pQueue, queueFamilyIndex);
        }
    }

    static void GetDeviceQueue2(const vkroots::VkDeviceDispatch* pDispatch,
                                VkDevice device,
                                const VkDeviceQueueInfo2* pQueueInfo,
                                VkQueue* pQueue)
    {
        pDispatch->GetDeviceQueue2(device, pQueueInfo, pQueue);
        if (!layer) layer = std::make_unique<Layer>();
        {
            std::lock_guard lock(layer->q_family_mtx);
            layer->queue_family.try_emplace(*pQueue, pQueueInfo->queueFamilyIndex);
        }
    }

    static void DestroyDevice(const vkroots::VkDeviceDispatch* d, VkDevice device, const VkAllocationCallbacks* pAllocator) {
        // Keep the global layer alive when unrelated temporary devices are destroyed.
        const bool destroy_layer = layer && layer->ovl_res && layer->ovl_res->d && layer->ovl_res->d->Device == device;
        d->DeviceWaitIdle(device);
        if (destroy_layer) {
            layer.reset();
        }

#ifdef VK_EXT_present_timing
        PresentationTiming::destroy_device(device);
#endif
        d->DestroyDevice(device, pAllocator);
    }

    template<typename Acquire>
    static VkResult acquire_limited(VkSwapchainKHR swapchain, uint64_t timeout,
                                    Acquire&& driver) {
        const float fps = layer ? layer->ipc->fps_limit.load(std::memory_order_relaxed) : 0;
        const int64_t interval = fps > 0 ? static_cast<int64_t>(1'000'000'000.0 / fps) : 0;
        if (interval <= 0 || timeout == 0)
            return driver(timeout);

        auto sc = layer->get_swapchain_data(swapchain);
#ifdef VK_EXT_present_timing
        if (sc->lsfg_requires_vsync)
            return driver(timeout);
#endif
        return sc->limiter.acquire(interval, timeout, std::forward<Acquire>(driver));
    }

    static VkResult AcquireNextImageKHR(const vkroots::VkDeviceDispatch* d,
                                       VkDevice device, VkSwapchainKHR swapchain,
                                       uint64_t timeout, VkSemaphore semaphore,
                                       VkFence fence, uint32_t* image_index) {
        return acquire_limited(swapchain, timeout, [&](uint64_t remaining) {
            return d->AcquireNextImageKHR(device, swapchain, remaining,
                                          semaphore, fence, image_index);
        });
    }

    static VkResult AcquireNextImage2KHR(const vkroots::VkDeviceDispatch* d,
                                        VkDevice device, const VkAcquireNextImageInfoKHR* info,
                                        uint32_t* image_index) {
        return acquire_limited(info->swapchain, info->timeout, [&](uint64_t remaining) {
            VkAcquireNextImageInfoKHR limited = *info;
            limited.timeout = remaining;
            return d->AcquireNextImage2KHR(device, &limited, image_index);
        });
    }
};

#ifdef VK_EXT_present_timing
static VKAPI_ATTR VkResult VKAPI_CALL set_timing_queue_size(
    VkDevice device, VkSwapchainKHR swapchain, uint32_t size) {
    return VkDeviceOverrides::SetSwapchainPresentTimingQueueSizeEXT(
        vkroots::tables::LookupDeviceDispatch(device), device, swapchain, size);
}

static VKAPI_ATTR VkResult VKAPI_CALL get_past_presentation_timing(
    VkDevice device, const VkPastPresentationTimingInfoEXT* info,
    VkPastPresentationTimingPropertiesEXT* properties) {
    return VkDeviceOverrides::GetPastPresentationTimingEXT(
        vkroots::tables::LookupDeviceDispatch(device), device, info, properties);
}

static PFN_vkVoidFunction timing_entry_point(const char* name) {
    if (!std::strcmp(name, "vkSetSwapchainPresentTimingQueueSizeEXT"))
        return reinterpret_cast<PFN_vkVoidFunction>(set_timing_queue_size);
    if (!std::strcmp(name, "vkGetPastPresentationTimingEXT"))
        return reinterpret_cast<PFN_vkVoidFunction>(get_past_presentation_timing);
    return nullptr;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL timing_device_proc_addr(VkDevice device, const char* name) {
    const auto* dispatch = vkroots::tables::LookupDeviceDispatch(device);
    if (dispatch && dispatch->GetDeviceProcAddr(device, name)) {
        if (auto entry = timing_entry_point(name))
            return entry;
    }
    if (!std::strcmp(name, "vkGetDeviceProcAddr"))
        return reinterpret_cast<PFN_vkVoidFunction>(timing_device_proc_addr);
    return vkroots::GetDeviceProcAddr<VkInstanceOverrides, vkroots::NoOverrides, VkDeviceOverrides>(device, name);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL timing_instance_proc_addr(VkInstance instance, const char* name) {
    if (instance) {
        const auto* dispatch = vkroots::tables::LookupInstanceDispatch(instance);
        if (dispatch && dispatch->GetInstanceProcAddr(instance, name)) {
            if (auto entry = timing_entry_point(name))
                return entry;
        }
        if (!std::strcmp(name, "vkGetDeviceProcAddr"))
            return reinterpret_cast<PFN_vkVoidFunction>(timing_device_proc_addr);
        if (!std::strcmp(name, "vkGetInstanceProcAddr"))
            return reinterpret_cast<PFN_vkVoidFunction>(timing_instance_proc_addr);
    }
    return vkroots::GetInstanceProcAddr<VkInstanceOverrides, vkroots::NoOverrides, VkDeviceOverrides>(instance, name);
}
#endif

// An empty override type keeps vkroots' loader/dispatch plumbing enabled
// without intercepting application calls. NoOverrides disables that plumbing.
struct ForwardingOverrides {};

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* version)
{
    if (IPCClient::should_disable())
        return vkroots::NegotiateLoaderLayerInterfaceVersion<
            ForwardingOverrides, vkroots::NoOverrides, ForwardingOverrides>(version);

    const auto result = vkroots::NegotiateLoaderLayerInterfaceVersion<
        VkInstanceOverrides, vkroots::NoOverrides, VkDeviceOverrides>(version);
#ifdef VK_EXT_present_timing
    if (result == VK_SUCCESS) {
        version->pfnGetInstanceProcAddr = timing_instance_proc_addr;
        version->pfnGetDeviceProcAddr = timing_device_proc_addr;
    }
#endif
    return result;
}

void Layer::init_overlay_resources(const VkSwapchainCreateInfoKHR* pCreateInfo, const vkroots::VkDeviceDispatch* pDispatch, uint32_t image_count) {
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
