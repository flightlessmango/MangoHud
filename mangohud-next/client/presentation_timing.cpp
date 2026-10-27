#include "presentation_timing.h"
#include "layer.h"
#include "mesa/os_time.h"
#include <array>
#include <chrono>
#include <pthread.h>
#include <cstring>

#ifdef VK_EXT_present_timing

VkResult PresentationTiming::create_device(
    const vkroots::VkInstanceDispatch* dispatch, VkPhysicalDevice physicalDevice,
    VkDeviceCreateInfo ci, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice,
    std::vector<const char*>& exts, void* newPNext) {
    auto add = [&](const char* name) {
        for (const auto* extension : exts)
            if (std::strcmp(extension, name) == 0)
                return;
        exts.push_back(name);
    };
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
    LSFGVKMangoHudInfo lsfg{};
    {
        auto& state = registry();
        std::lock_guard lock(state.m);
        auto it = state.instances.find(dispatch->Instance);
        if (it != state.instances.end())
            lsfg = it->second;
    }
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
                const auto* existing = reinterpret_cast<const VkPhysicalDevicePresentTimingFeaturesEXT*>(it);
                timing_enabled = existing->presentTiming;
            }
        }
        if (timing_enabled) {
            add(VK_EXT_PRESENT_TIMING_EXTENSION_NAME);
            add(VK_KHR_PRESENT_ID_2_EXTENSION_NAME);
            add(VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
            if (!has_type(newPNext, timing.sType)) {
                timing.presentAtAbsoluteTime = VK_FALSE;
                timing.presentAtRelativeTime = VK_FALSE;
                timing.pNext = newPNext;
                newPNext = &timing;
            }
        }
    }
    ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    ci.pNext = newPNext;
    auto result = dispatch->CreateDevice(physicalDevice, &ci, pAllocator, pDevice);
    if (result == VK_SUCCESS) {
        auto& state = registry();
        std::lock_guard lock(state.m);
        state.devices[*pDevice] = {timing_enabled, lsfg};
    }
    return result;
}

VkResult PresentationTiming::create_instance(
    PFN_vkCreateInstance pfnCreateInstance, VkInstanceCreateInfo ci,
    const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    LSFGVKMangoHudInfo lsfg{};
    for (auto* it = static_cast<const VkBaseInStructure*>(ci.pNext); it; it = it->pNext) {
        if (static_cast<uint32_t>(it->sType) == LSFGVK_STRUCTURE_TYPE_CREATE_INFO) {
            const auto* info = reinterpret_cast<const LSFGVKCreateInfo*>(it);
            if (info->mangohud_info)
                lsfg = *info->mangohud_info;
        }
    }
    LSFGVKMangoHudInfo queried{};
    LSFGVKCreateQuery query{LSFGVK_STRUCTURE_TYPE_CREATE_QUERY, const_cast<void*>(ci.pNext), &queried};
    ci.pNext = &query;
    auto result = pfnCreateInstance(&ci, pAllocator, pInstance);
    if (result == VK_SUCCESS) {
        if (queried.isPresent)
            lsfg = queried;
        auto& state = registry();
        std::lock_guard lock(state.m);
        state.instances[*pInstance] = lsfg;
        SPDLOG_DEBUG("lsfg: present={} requires_vsync={}",
                     lsfg.isPresent, lsfg.requiresVsync);
    }
    return result;
}

void PresentationTiming::destroy_instance(VkInstance instance) {
    auto& state = registry();
    std::lock_guard lock(state.m);
    state.instances.erase(instance);
}

void PresentationTiming::destroy_device(VkDevice device) {
    auto& state = registry();
    std::lock_guard lock(state.m);
    state.devices.erase(device);
}

swapchain_timing_config PresentationTiming::prepare_swapchain(
    const vkroots::VkDeviceDispatch* pDispatch, VkSwapchainCreateInfoKHR& create_info) {
    swapchain_timing_config config;
    {
        auto& state = registry();
        std::lock_guard lock(state.m);
        auto it = state.devices.find(pDispatch->Device);
        if (it != state.devices.end()) {
            config.lsfg_is_present = it->second.lsfg.isPresent;
            config.lsfg_requires_vsync = it->second.lsfg.requiresVsync;
            config.enabled = it->second.enabled;
        }
    }
    constexpr VkPresentStageFlagsEXT requested_stages = VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT |
                                                       VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT;
    auto get_caps = pDispatch->pPhysicalDeviceDispatch->pInstanceDispatch->GetPhysicalDeviceSurfaceCapabilities2KHR;
    if (config.enabled && get_caps) {
        VkPhysicalDeviceSurfaceInfo2KHR surface_info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR};
        surface_info.surface = create_info.surface;
        VkPresentTimingSurfaceCapabilitiesEXT timing_caps{VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT};
        VkSurfaceCapabilities2KHR caps{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR};
        caps.pNext = &timing_caps;
        config.enabled = get_caps(pDispatch->PhysicalDevice, &surface_info, &caps) == VK_SUCCESS &&
                         timing_caps.presentTimingSupported;
        config.stage = timing_caps.presentStageQueries & requested_stages;
    }
    config.enabled = config.enabled && config.stage != 0;
    if (config.enabled)
        create_info.flags |= VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT;
    return config;
}

void PresentationTiming::init_swapchain(
    const vkroots::VkDeviceDispatch* pDispatch, VkDevice device, VkSwapchainKHR swapchain,
    const std::shared_ptr<swapchain_data>& sc, swapchain_timing_config config) {
    sc->lsfg_is_present = config.lsfg_is_present;
    sc->lsfg_requires_vsync = config.lsfg_requires_vsync;
    if (config.enabled) {
        auto& timing = sc->timing;
        std::lock_guard lock(timing.m);
        timing.ipc = layer.ipc.get();
        timing.device = device;
        timing.swapchain = swapchain;
        timing.stage = config.stage;
        timing.get_domains = reinterpret_cast<PFN_vkGetSwapchainTimeDomainPropertiesEXT>(
            pDispatch->GetDeviceProcAddr(device, "vkGetSwapchainTimeDomainPropertiesEXT"));
        timing.get_results = reinterpret_cast<PFN_vkGetPastPresentationTimingEXT>(
            pDispatch->GetDeviceProcAddr(device, "vkGetPastPresentationTimingEXT"));
        timing.set_queue = reinterpret_cast<PFN_vkSetSwapchainPresentTimingQueueSizeEXT>(
            pDispatch->GetDeviceProcAddr(device, "vkSetSwapchainPresentTimingQueueSizeEXT"));
        timing.enabled = timing.get_results && timing.set_queue && timing.update_domain() &&
            timing.set_queue(device, swapchain, timing.driver_queue_size.load(std::memory_order_relaxed)) == VK_SUCCESS;
        timing.managed = timing.enabled;
        config.enabled = timing.enabled;
    }
    if (config.enabled)
        start();
    SPDLOG_DEBUG("frame timing: presentation feedback {} for swapchain 0x{:x}, stage=0x{:x}",
                 config.enabled ? "enabled" : "unavailable", (uint64_t)swapchain, config.stage);
}

bool PresentationTiming::check_present(
    const std::shared_ptr<swapchain_data>& sc, const VkPresentInfoKHR* pPresentInfo) {
    bool feedback = false;
    if (sc && pPresentInfo->swapchainCount == 1) {
        const bool allow_timing = sc->timing.allow_timing(
            static_cast<uint64_t>(os_time_get_nano()), sc->lsfg_is_present);
        feedback = sc->timing.enabled && allow_timing;
    }
    return feedback;
}

VkResult PresentationTiming::present(
    const vkroots::VkDeviceDispatch* pDispatch, VkQueue queue, const VkPresentInfoKHR* info,
    const std::shared_ptr<swapchain_data>& sc, bool feedback, bool timing_fps) {
    for (auto* it = static_cast<const VkBaseInStructure*>(info->pNext); it; it = it->pNext) {
        if (it->sType != VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT)
            continue;
        const auto* timings = reinterpret_cast<const VkPresentTimingsInfoEXT*>(it);
        const auto result = pDispatch->QueuePresentKHR(queue, info);
        for (uint32_t i = 0; i < info->swapchainCount; ++i) {
            const auto stages = timings->pTimingInfos[i].presentStageQueries;
            const auto presented = info->pResults ? info->pResults[i] : result;
            auto swap = i == 0 ? sc : layer.get_swapchain_data(info->pSwapchains[i]);
            if (swap && swap->timing.managed && stages &&
                (presented == VK_SUCCESS || presented == VK_SUBOPTIMAL_KHR))
                swap->timing.record_presents(1, stages);
        }
        return result;
    }
    if (feedback) {
        VkPresentTimingInfoEXT timing{VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT};
        VkPresentTimingsInfoEXT timings{VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT};
        if (!sc->timing.enabled)
            return pDispatch->QueuePresentKHR(queue, info);
        const auto pending = sc->timing.pending.load(std::memory_order_relaxed);
        const auto expected_presents = sc->timing.presents_per_frame.load(std::memory_order_relaxed);
        const auto queue_size = sc->timing.driver_queue_size.load(std::memory_order_relaxed);
        if (pending + expected_presents > queue_size) {
            if (!sc->timing.queue_limit_reported.exchange(true, std::memory_order_relaxed))
                SPDLOG_ERROR("Presentation timing queue capacity exceeded: pending={}, expected presents={}, limit={}; skipping timing requests",
                             pending, expected_presents, queue_size);
            return pDispatch->QueuePresentKHR(queue, info);
        }
        timing.timeDomainId = sc->timing.domain;
        timing.presentStageQueries = sc->timing.stage;
        timings.pNext = info->pNext;
        timings.swapchainCount = 1;
        timings.pTimingInfos = &timing;
        VkPresentInfoKHR timed = *info;
        uint32_t presents = 1;
        LSFGVKMangoHudPresentQuery request{1, &timing.timeDomainId, &timing.presentStageQueries};
        LSFGVKPresentQuery query{LSFGVK_STRUCTURE_TYPE_PRESENT_QUERY,
                                &timings, 1, &presents, &request};
        timed.pNext = sc->lsfg_is_present ?
            static_cast<const void*>(&query) : &timings;
        auto result = pDispatch->QueuePresentKHR(queue, &timed);
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
            sc->timing.record_presents(presents, 0, timing_fps);
        return result;
    }
    return pDispatch->QueuePresentKHR(queue, info);
}

PresentationTiming::~PresentationTiming() {
    stopping = true;
    if (thread.joinable())
        thread.join();
}

void PresentationTiming::start() {
    std::lock_guard lock(thread_m);
    if (thread.joinable())
        return;
    thread = std::thread([this] {
        pthread_setname_np(pthread_self(), "mangohud_timing");
        while (!stopping) {
            {
                std::lock_guard lock(m);
                std::vector<std::shared_ptr<swapchain_data>> swaps;
                {
                    std::shared_lock lock(layer.swapchain_mtx);
                    for (const auto& [handle, sc] : layer.swapchains)
                        if (sc->timing.enabled)
                            swaps.push_back(sc);
                }
                for (const auto& sc : swaps) {
                    std::lock_guard lock(sc->timing.m);
                    sc->timing.drain();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    });
}

void PresentationTiming::retire(VkSwapchainKHR swapchain) {
    std::lock_guard lifetime_lock(m);
    auto sc = layer.get_swapchain_data(swapchain);
    if (!sc)
        return;
    std::lock_guard lock(sc->timing.m);
    sc->timing.enabled = false;
}

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
        last_queue_timestamp = 0;
        last_app_timestamp = 0;
    }
    domain = ids[0];
    return true;
}

VkResult presentation_timing::drain() {
    if (!managed || pending <= 0)
        return VK_SUCCESS;
    if (application_queue_size && application_reports.size() >= application_queue_size)
        return VK_SUCCESS;
    const auto queue_size = driver_queue_size.load(std::memory_order_relaxed);
    std::vector<VkPastPresentationTimingEXT> results(queue_size);
    std::vector<std::array<VkPresentStageTimeEXT, 4>> stages(queue_size);
    for (uint32_t i = 0; i < queue_size; ++i) {
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
        return result;
    }
    timing_properties_counter = properties.timingPropertiesCounter;
    time_domains_counter = properties.timeDomainsCounter;
    for (uint32_t i = 0; i < properties.presentationTimingCount; ++i) {
        const auto& report = results[i];
        if (!report.reportComplete)
            continue;
        Report collected{report, {}};
        std::copy_n(report.pPresentStages, report.presentStageCount, collected.stages.begin());
        collected_reports.push_back(collected);
    }
    while (!collected_reports.empty()) {
        Request request;
        {
            std::lock_guard lock(requests_m);
            if (requests.empty())
                break;
            request = requests.front();
            requests.pop_front();
        }
        --pending;
        auto collected = collected_reports.front();
        collected_reports.pop_front();
        if (request.application_stages)
            application_reports.push_back(collected);
        const auto& report = collected.value;
        if (report.timeDomainId != domain) {
            domain = report.timeDomainId;
            timestamp_origin = 0;
            last_queue_timestamp = 0;
            last_app_timestamp = 0;
        }
        for (uint32_t j = 0; j < report.presentStageCount; ++j) {
            const auto timestamp = collected.stages[j].time;
            const auto report_stage = collected.stages[j].stage & stage;
            if (!report_stage || timestamp == 0)
                continue;
            if (timestamp_origin == 0) {
                timestamp_origin = timestamp;
                sample_origin = os_time_get_nano();
            }
            const auto sample_ns = sample_origin + timestamp - timestamp_origin;
            if (request.timing_fps &&
                (report_stage & VK_PRESENT_STAGE_QUEUE_OPERATIONS_END_BIT_EXT) &&
                timestamp > last_queue_timestamp) {
                last_queue_timestamp = timestamp;
                ipc->add_to_queue(sample_ns);
            }
            if ((report_stage & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT) &&
                timestamp > last_app_timestamp) {
                last_app_timestamp = timestamp;
                static std::atomic<uint64_t> app_seq{0};
                ipc->add_to_queue(SampleType::App, app_seq.fetch_add(1, std::memory_order_relaxed), sample_ns);
            }
        }
    }
    if (domain_counter != properties.timeDomainsCounter && !update_domain())
        enabled = false;
    return result;
}

VkResult presentation_timing::set_application_queue_size(uint32_t size) {
    {
        std::lock_guard lock(requests_m);
        const auto outstanding = application_reports.size() +
            std::count_if(requests.begin(), requests.end(), [](const auto& request) { return request.application_stages != 0; });
        if (size < outstanding)
            return VK_NOT_READY;
    }
    const auto actual_size = std::max(size, driver_queue_size.load(std::memory_order_relaxed));
    const auto result = set_queue(device, swapchain, actual_size);
    if (result == VK_SUCCESS) {
        application_queue_size = size;
        driver_queue_size.store(actual_size, std::memory_order_relaxed);
    }
    return result;
}

VkResult presentation_timing::get_application_results(
    VkPastPresentationTimingPropertiesEXT* properties) {
    const auto result = drain();
    if (result < 0)
        return result;
    if (pending <= 0) {
        VkPastPresentationTimingInfoEXT query{VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT};
        query.swapchain = swapchain;
        VkPastPresentationTimingPropertiesEXT counters{VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT};
        const auto result = get_results(device, &query, &counters);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE)
            return result;
        timing_properties_counter = counters.timingPropertiesCounter;
        time_domains_counter = counters.timeDomainsCounter;
    }
    properties->timingPropertiesCounter = timing_properties_counter;
    properties->timeDomainsCounter = time_domains_counter;
    if (!properties->pPresentationTimings) {
        properties->presentationTimingCount = application_reports.size();
        return VK_SUCCESS;
    }
    const auto count = std::min<size_t>(properties->presentationTimingCount, application_reports.size());
    for (uint32_t i = 0; i < count; ++i) {
        const auto& report = application_reports.front();
        auto& output = properties->pPresentationTimings[i];
        auto* stages = output.pPresentStages;
        auto* next = output.pNext;
        output = report.value;
        output.pNext = next;
        output.pPresentStages = stages;
        std::copy_n(report.stages.begin(), output.presentStageCount, stages);
        application_reports.pop_front();
    }
    properties->presentationTimingCount = count;
    return application_reports.empty() ? VK_SUCCESS : VK_INCOMPLETE;
}
#endif
