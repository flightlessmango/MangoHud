#define VKROOTS_LAYER_IMPLEMENTATION
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <array>

#include "vkroots.h"
#include "mesa/os_time.h"
#include "fps_limiter.h"
#include "layer.h"
#include "file_utils.h"
#include "wayland.h"
#include "x11.h"

static char pendingEngineName[VK_MAX_DESCRIPTION_SIZE]{};
std::unique_ptr<fpsLimiter> fps_limiter;
std::unique_ptr<presentLimiter> present_limiter;
std::unique_ptr<Layer> layer;
static std::unique_ptr<Wayland> wayland;
static std::unique_ptr<X11> x11;


#ifdef VK_EXT_present_timing
static std::mutex timing_devices_m;
static std::unordered_map<VkDevice, bool> timing_devices;

static constexpr uint32_t timing_queue_size = 256;

bool presentation_timing::update_domain() {
    VkSwapchainTimeDomainPropertiesEXT properties{VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT};
    if (!get_domains || get_domains(device, swapchain, &properties, &domain_counter) != VK_SUCCESS ||
        properties.timeDomainCount == 0)
        return false;
    std::vector<VkTimeDomainKHR> types(properties.timeDomainCount);
    std::vector<uint64_t> ids(properties.timeDomainCount);
    properties.pTimeDomains = types.data();
    properties.pTimeDomainIds = ids.data();
    if (get_domains(device, swapchain, &properties, &domain_counter) != VK_SUCCESS)
        return false;
    if (properties.timeDomainCount == 0)
        return false;
    if (domain != ids[0]) {
        timestamp_origin = 0;
        last_timestamp = 0;
    }
    domain = ids[0];
    return true;
}

void presentation_timing::drain(IPCClient& ipc) {
    if (!enabled || pending == 0)
        return;
    std::array<VkPastPresentationTimingEXT, timing_queue_size> results{};
    std::array<std::array<VkPresentStageTimeEXT, 4>, timing_queue_size> stages{};
    for (uint32_t i = 0; i < timing_queue_size; ++i) {
        results[i].sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_EXT;
        results[i].presentStageCount = stages[i].size();
        results[i].pPresentStages = stages[i].data();
    }
    VkPastPresentationTimingInfoEXT query{VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT};
    query.swapchain = swapchain;
    VkPastPresentationTimingPropertiesEXT properties{VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT};
    properties.presentationTimingCount = results.size();
    properties.pPresentationTimings = results.data();
    auto result = get_results(device, &query, &properties);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
        SPDLOG_DEBUG("presentation timing query failed: {}", string_VkResult(result));
        enabled = false;
        return;
    }
    for (uint32_t i = 0; i < properties.presentationTimingCount; ++i) {
        const auto& report = results[i];
        if (!report.reportComplete)
            continue;
        if (pending > 0)
            --pending;
        for (uint32_t j = 0; j < report.presentStageCount; ++j) {
            auto timestamp = report.pPresentStages[j].time;
            if (report.pPresentStages[j].stage != stage || timestamp == 0)
                continue;
            if (report.timeDomainId != domain) {
                domain = report.timeDomainId;
                timestamp_origin = 0;
                last_timestamp = 0;
            }
            if (timestamp <= last_timestamp)
                continue;
            last_timestamp = timestamp;
            if (timestamp_origin == 0) {
                timestamp_origin = timestamp;
                sample_origin = os_time_get_nano();
                SPDLOG_DEBUG("frame timing: first presentation timestamp={} domain={} stage=0x{:x}",
                             timestamp, domain, stage);
            }
            ipc.add_to_queue(sample_origin + timestamp - timestamp_origin);
        }
    }
    if (domain_counter != properties.timeDomainsCounter && !update_domain())
        enabled = false;
}
#endif

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
        VkPhysicalDevicePresentTimingFeaturesEXT timing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT};
        uint32_t extension_count = 0;
        std::vector<VkExtensionProperties> available;
        if (dispatch->EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extension_count, nullptr) == VK_SUCCESS) {
            available.resize(extension_count);
            if (dispatch->EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extension_count, available.data()) != VK_SUCCESS)
                available.clear();
        }
        auto has_extension = [&](const char* name) {
            for (const auto& extension : available)
                if (std::strcmp(extension.extensionName, name) == 0)
                    return true;
            return false;
        };
        bool timing_enabled = false;
        if (has_extension(VK_EXT_PRESENT_TIMING_EXTENSION_NAME) &&
            has_extension(VK_KHR_PRESENT_ID_2_EXTENSION_NAME) &&
            has_extension(VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)) {
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &timing;
            if (dispatch->GetPhysicalDeviceFeatures2)
                dispatch->GetPhysicalDeviceFeatures2(physicalDevice, &features);
            else {
                auto get_features = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2KHR>(
                    dispatch->GetInstanceProcAddr(dispatch->Instance, "vkGetPhysicalDeviceFeatures2KHR"));
                if (get_features)
                    get_features(physicalDevice, &features);
            }
            timing_enabled = timing.presentTiming;
            for (auto* it = static_cast<const VkBaseInStructure*>(ci.pNext); it; it = it->pNext) {
                if (it->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT) {
                    timing_enabled = false;
                }
            }
            if (timing_enabled) {
                add(VK_EXT_PRESENT_TIMING_EXTENSION_NAME);
                add(VK_KHR_PRESENT_ID_2_EXTENSION_NAME);
                add(VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
                if (!ChainHasSType(newPNext, timing.sType)) {
                    timing.presentAtAbsoluteTime = VK_FALSE;
                    timing.presentAtRelativeTime = VK_FALSE;
                    timing.pNext = newPNext;
                    newPNext = &timing;
                }
            }
        }
        ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
        ci.ppEnabledExtensionNames = exts.data();
#endif
        ci.pNext = newPNext;
        auto result = dispatch->CreateDevice(physicalDevice, &ci, pAllocator, pDevice);
#ifdef VK_EXT_present_timing
        if (result == VK_SUCCESS) {
            std::lock_guard lock(timing_devices_m);
            timing_devices[*pDevice] = timing_enabled;
        }
#endif
        return result;
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

        return pfnCreateInstance(&ci, pAllocator, pInstance);
    }

    static VkResult CreateWaylandSurfaceKHR(const vkroots::VkInstanceDispatch* dispatch, VkInstance instance,
                                     const VkWaylandSurfaceCreateInfoKHR *pCreateInfo,
                                     const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
    {
        VkResult r = dispatch->CreateWaylandSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
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

static const VkPresentIdKHR* GetPresentId(const void* pNext) {
    for (auto* it = (const VkBaseInStructure*)pNext; it; it = it->pNext) {
        if (it->sType == VK_STRUCTURE_TYPE_PRESENT_ID_KHR) {
            return (const VkPresentIdKHR*)it;
        }
    }
    return nullptr;
}

class VkDeviceOverrides {
public:
    static VkResult CreateSwapchainKHR(
        const vkroots::VkDeviceDispatch* pDispatch,
        VkDevice device,
        const VkSwapchainCreateInfoKHR* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkSwapchainKHR* pSwapchain)
    {
        VkSwapchainCreateInfoKHR create_info = *pCreateInfo;
#ifdef VK_EXT_present_timing
        bool timing_enabled = false;
        VkPresentStageFlagsEXT timing_stage = 0;
        {
            std::lock_guard lock(timing_devices_m);
            auto it = timing_devices.find(device);
            timing_enabled = it != timing_devices.end() && it->second;
        }
        auto get_caps = pDispatch->pPhysicalDeviceDispatch->pInstanceDispatch->GetPhysicalDeviceSurfaceCapabilities2KHR;
        if (timing_enabled && get_caps) {
            VkPhysicalDeviceSurfaceInfo2KHR surface_info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR};
            surface_info.surface = pCreateInfo->surface;
            VkPresentTimingSurfaceCapabilitiesEXT timing_caps{VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT};
            VkSurfaceCapabilities2KHR caps{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR};
            caps.pNext = &timing_caps;
            timing_enabled = get_caps(pDispatch->PhysicalDevice, &surface_info, &caps) == VK_SUCCESS &&
                             timing_caps.presentTimingSupported;
            if (timing_caps.presentStageQueries & VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT)
                timing_stage = VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT;
        }
        timing_enabled = timing_enabled && timing_stage != 0;
        if (timing_enabled)
            create_info.flags |= VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT;
#endif
        VkResult r = pDispatch->CreateSwapchainKHR(pDispatch->Device, &create_info, pAllocator, pSwapchain);
#ifdef VK_EXT_present_timing
        if (r != VK_SUCCESS && timing_enabled && create_info.flags != pCreateInfo->flags) {
            timing_enabled = false;
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
        if (timing_enabled) {
            auto sc = layer->get_swapchain_data(*pSwapchain);
            auto& timing = sc->timing;
            timing.device = device;
            timing.swapchain = *pSwapchain;
            timing.stage = timing_stage;
            timing.get_domains = reinterpret_cast<PFN_vkGetSwapchainTimeDomainPropertiesEXT>(
                pDispatch->GetDeviceProcAddr(device, "vkGetSwapchainTimeDomainPropertiesEXT"));
            timing.get_results = reinterpret_cast<PFN_vkGetPastPresentationTimingEXT>(
                pDispatch->GetDeviceProcAddr(device, "vkGetPastPresentationTimingEXT"));
            auto set_queue = reinterpret_cast<PFN_vkSetSwapchainPresentTimingQueueSizeEXT>(
                pDispatch->GetDeviceProcAddr(device, "vkSetSwapchainPresentTimingQueueSizeEXT"));
            timing.enabled = timing.get_results && set_queue && timing.update_domain() &&
                set_queue(device, *pSwapchain, timing_queue_size) == VK_SUCCESS;
            timing_enabled = timing.enabled;
        }
        SPDLOG_DEBUG("frame timing: presentation feedback {} for swapchain 0x{:x}, stage=0x{:x}",
                     timing_enabled ? "enabled" : "unavailable", (uint64_t)*pSwapchain, timing_stage);
#endif
        layer->ipc->send_resolution(pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height);

        layer->g_vkSetDebugUtilsObjectNameEXT =
        reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(pDispatch->pPhysicalDeviceDispatch->Instance, "vkSetDebugUtilsObjectNameEXT"));
        return VK_SUCCESS;
    }

    static void DestroySwapchainKHR(
    const vkroots::VkDeviceDispatch* pDispatch,
    VkDevice device,
    VkSwapchainKHR swapchain,
    const VkAllocationCallbacks* pAllocator)
    {
        {
            std::lock_guard lock(layer->swapchain_mtx);
            auto it = layer->swapchains.find(swapchain);
            if (it != layer->swapchains.end())
                layer->swapchains.erase(it);
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
        if (sc) {
            std::lock_guard lock(sc->m);
            if (pPresentInfo->swapchainCount != 1)
                sc->timing.enabled = false;
            for (auto* it = static_cast<const VkBaseInStructure*>(pPresentInfo->pNext); it; it = it->pNext)
                if (it->sType == VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT)
                    sc->timing.enabled = false;
            sc->timing.drain(*layer->ipc);
            feedback = sc->timing.enabled;
        }
#endif
        if (layer && !feedback)
            layer->ipc->add_to_queue(os_time_get_nano());
        auto present = [&](const VkPresentInfoKHR* info) {
#ifdef VK_EXT_present_timing
            if (feedback) {
                VkPresentTimingInfoEXT timing{VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT};
                VkPresentTimingsInfoEXT timings{VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT};
                {
                    std::lock_guard lock(sc->m);
                    if (sc->timing.pending >= timing_queue_size)
                        return pDispatch->QueuePresentKHR(queue, info);
                    timing.timeDomainId = sc->timing.domain;
                    timing.presentStageQueries = sc->timing.stage;
                }
                timings.pNext = info->pNext;
                timings.swapchainCount = 1;
                timings.pTimingInfos = &timing;
                VkPresentInfoKHR timed = *info;
                timed.pNext = &timings;
                auto result = pDispatch->QueuePresentKHR(queue, &timed);
                if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
                    std::lock_guard lock(sc->m);
                    ++sc->timing.pending;
                }
                return result;
            }
#endif
            return pDispatch->QueuePresentKHR(queue, info);
        };
        if (!sc)
            return present(pPresentInfo);
        if (x11) x11->dispatch_events();
        if (wayland) {
            auto swapchain_data = layer->get_swapchain_data(pPresentInfo->pSwapchains[0]);
            wayland->ensure_overlay(swapchain_data->vk_surface);
            wayland->request_presentation_feedback(swapchain_data->vk_surface);
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
            std::lock_guard lock(layer->swapchain_mtx);
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

        if (!fps_limiter)
            fps_limiter = std::make_unique<fpsLimiter>(false);

        {
            std::lock_guard lock(fps_limiter->q_limiter->present_queues_mtx);
            fps_limiter->q_limiter->present_queues.insert(queue);
        }

        fps_limiter->limit(true);
        // TODO Probably don't do this every frame
        fps_limiter->set_fps_limit(layer->ipc->fps_limit);

        layer->ipc->start(swapchain_image_count);
        bool drew;
        {
            std::lock_guard lock(layer->overlay_vk->m);
            drew = layer->overlay_vk->draw(pPresentInfo->pSwapchains[0], imageIndex, queue, pi);
        }
        if (!drew)
            return present(pPresentInfo);

        if (!present_limiter)
            present_limiter = std::make_unique<presentLimiter>(pDispatch->WaitForPresentKHR);

        const VkPresentIdKHR* existing_pid = GetPresentId(pi.pNext);

        static thread_local std::vector<uint64_t> tl_ids;
        static thread_local VkPresentIdKHR tl_pid;

        const uint64_t* ids_ptr = nullptr;

        if (existing_pid && existing_pid->pPresentIds && existing_pid->swapchainCount == pi.swapchainCount) {
            ids_ptr = existing_pid->pPresentIds;
        } else {
            tl_ids.resize(pi.swapchainCount);
            present_limiter->on_present(&pi, tl_ids.data());

            tl_pid = {};
            tl_pid.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
            tl_pid.swapchainCount = pi.swapchainCount;
            tl_pid.pPresentIds = tl_ids.data();
            tl_pid.pNext = (void*)pi.pNext;
            pi.pNext = &tl_pid;

            ids_ptr = tl_ids.data();
        }

        VkSemaphore signal;
        {
            std::lock_guard lock(layer->overlay_vk->m);
            signal = layer->ovl_res->overlay_done[imageIndex];
        }

        VkPresentInfoKHR pi2 = *pPresentInfo;
        pi2.waitSemaphoreCount = 1;
        pi2.pWaitSemaphores = &signal;

        VkResult r = present(&pi2);

        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
            if (ids_ptr)
                present_limiter->on_present_result(&pi, ids_ptr, r);


            if (fps_limiter && fps_limiter->active)
                present_limiter->throttle(pDispatch->Device, pi.pSwapchains[0], 1);
        }

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
            fps_limiter.reset();
        }

#ifdef VK_EXT_present_timing
        {
            std::lock_guard lock(timing_devices_m);
            timing_devices.erase(device);
        }
#endif
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

        if (r != VK_SUCCESS) {
            SPDLOG_ERROR("QueueSubmit {}", string_VkResult(r));
            return r;
        }

        if (q_limiter->is_present_queue(queue)) {
            VkResult r2 = q_limiter->mark_after_submit(d, queue);
            if (r2 != VK_SUCCESS) {
                SPDLOG_ERROR("QueueSubmit limiter mark_after_submit {}", string_VkResult(r2));
                return r2;
            }
        }

        return r;
    }

    static VkResult AcquireNextImageKHR(const vkroots::VkDeviceDispatch* pDispatch,
                                        VkDevice device, VkSwapchainKHR swapchain,
                                        uint64_t timeout, VkSemaphore semaphore,
                                        VkFence fence, uint32_t *pImageIndex)
    {
        if (!fps_limiter)
            fps_limiter = std::make_unique<fpsLimiter>(false);

        VkResult r = pDispatch->AcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
            fps_limiter->limit(false);

        return r;
    }

    static VkResult AcquireNextImage2KHR(const vkroots::VkDeviceDispatch* pDispatch,
                                         VkDevice device, const VkAcquireNextImageInfoKHR *pAcquireInfo, uint32_t *pImageIndex)
    {
        if (!fps_limiter)
            fps_limiter = std::make_unique<fpsLimiter>(false);

        VkResult r = pDispatch->AcquireNextImage2KHR(device, pAcquireInfo, pImageIndex);
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
            fps_limiter->limit(false);

        return r;
    }
};

// An empty override type keeps vkroots' loader/dispatch plumbing enabled
// without intercepting application calls. NoOverrides disables that plumbing.
struct ForwardingOverrides {};

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* version)
{
    if (IPCClient::is_blacklisted())
        return vkroots::NegotiateLoaderLayerInterfaceVersion<
            ForwardingOverrides, vkroots::NoOverrides, ForwardingOverrides>(version);

    return vkroots::NegotiateLoaderLayerInterfaceVersion<
        VkInstanceOverrides, vkroots::NoOverrides, VkDeviceOverrides>(version);
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
