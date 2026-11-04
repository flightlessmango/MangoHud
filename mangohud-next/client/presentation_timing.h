#pragma once
#include <atomic>
#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "vulkan.h"

#ifdef VK_EXT_present_timing
#include "lsfg_api.h"

inline bool has_type(const void* head, VkStructureType type) {
    for (auto* it = static_cast<const VkBaseInStructure*>(head); it; it = it->pNext)
        if (it->sType == type)
            return true;
    return false;
}

class Layer;
struct swapchain_data;

struct swapchain_timing_config {
    bool enabled = false;
    bool lsfg_is_present = false;
    bool lsfg_requires_vsync = false;
    VkPresentStageFlagsEXT stage = 0;
};

struct presentation_timing {
    IPCClient* ipc = nullptr;
    std::mutex m;
    std::atomic<bool> enabled = false;
    bool managed = false;
    VkPresentStageFlagsEXT stage = 0;
    std::atomic<uint64_t> domain = 0;
    uint64_t domain_counter = 0;
    uint64_t timestamp_origin = 0;
    uint64_t sample_origin = 0;
    uint64_t last_queue_timestamp = 0;
    uint64_t last_app_timestamp = 0;
    std::atomic<int64_t> pending = 0;
    std::atomic<uint32_t> presents_per_frame = 1;
    std::atomic<bool> queue_limit_reported = false;
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    PFN_vkGetSwapchainTimeDomainPropertiesEXT get_domains = nullptr;
    PFN_vkGetPastPresentationTimingEXT get_results = nullptr;
    PFN_vkSetSwapchainPresentTimingQueueSizeEXT set_queue = nullptr;
    std::mutex requests_m;
    struct Request {
        VkPresentStageFlagsEXT application_stages;
        bool timing_fps;
    };
    std::deque<Request> requests;
    struct Report {
        VkPastPresentationTimingEXT value;
        std::array<VkPresentStageTimeEXT, 4> stages;
    };
    std::deque<Report> application_reports;
    std::deque<Report> collected_reports;
    uint32_t application_queue_size = 0;
    std::atomic<uint32_t> driver_queue_size{1024};
    uint64_t timing_properties_counter = 0;
    uint64_t time_domains_counter = 0;

    std::atomic<uint64_t> present_window_start = 0;
    std::atomic<uint64_t> present_count = 0;
    std::atomic<bool> high_fps = false;

    bool allow_timing(uint64_t now, bool lsfg_is_present) {
        auto start = present_window_start.load(std::memory_order_relaxed);
        if (start == 0) {
            present_window_start.compare_exchange_strong(start, now, std::memory_order_relaxed);
            return !high_fps.load(std::memory_order_relaxed);
        }
        present_count.fetch_add(1, std::memory_order_relaxed);
        if (now > start && now - start >= 500'000'000ULL &&
            present_window_start.compare_exchange_strong(start, now, std::memory_order_relaxed)) {
            const auto elapsed = now - start;
            const auto count = present_count.exchange(0, std::memory_order_relaxed);
            const bool high = count * 1'000'000'000ULL > elapsed * 2000;
            const bool low = count * 1'000'000'000ULL < elapsed * 1000;
            if ((high || low) && high_fps.exchange(high, std::memory_order_relaxed) != high && enabled) {
                if (high) {
                    if (lsfg_is_present)
                        SPDLOG_WARN("MangoHud presentation timing requests disabled above 2000 application FPS; LSFG frame timings may be inaccurate until FPS drops below 1000");
                    else
                        SPDLOG_INFO("MangoHud presentation timing requests disabled above 2000 application FPS");
                } else {
                    SPDLOG_INFO("MangoHud presentation timing requests resumed below 1000 application FPS");
                }
            }
        }
        return !high_fps.load(std::memory_order_relaxed);
    }

    void record_presents(uint32_t presents, VkPresentStageFlagsEXT application_stages = 0, bool timing_fps = false) {
        std::lock_guard lock(requests_m);
        requests.insert(requests.end(), presents, Request{application_stages, timing_fps});
        pending.fetch_add(presents);
        auto previous = presents_per_frame.load(std::memory_order_relaxed);
        while (previous < presents &&
               !presents_per_frame.compare_exchange_weak(previous, presents, std::memory_order_relaxed)) {}
    }

    bool update_domain();
    VkResult drain();
    VkResult set_application_queue_size(uint32_t size);
    VkResult get_application_results(VkPastPresentationTimingPropertiesEXT* properties);
};

class PresentationTiming {
public:
    explicit PresentationTiming(Layer& layer) : layer(layer) {}
    ~PresentationTiming();

    static VkResult create_instance(PFN_vkCreateInstance create, VkInstanceCreateInfo info,
                                    const VkAllocationCallbacks* allocator, VkInstance* instance);
    static void destroy_instance(VkInstance instance);
    static VkResult create_device(const vkroots::VkInstanceDispatch* dispatch,
                                  VkPhysicalDevice physical_device, VkDeviceCreateInfo info,
                                  const VkAllocationCallbacks* allocator, VkDevice* device,
                                  std::vector<const char*>& extensions, void* next);
    static void destroy_device(VkDevice device);
    static swapchain_timing_config prepare_swapchain(const vkroots::VkDeviceDispatch* dispatch,
                                                     VkSwapchainCreateInfoKHR& info);
    void init_swapchain(const vkroots::VkDeviceDispatch* dispatch, VkDevice device,
                        VkSwapchainKHR swapchain, const std::shared_ptr<swapchain_data>& sc,
                        swapchain_timing_config config);
    static bool check_present(const std::shared_ptr<swapchain_data>& sc, const VkPresentInfoKHR* info);
    VkResult present(const vkroots::VkDeviceDispatch* dispatch, VkQueue queue,
                            const VkPresentInfoKHR* info, const std::shared_ptr<swapchain_data>& sc,
                            bool feedback, bool timing_fps);

    void start();
    void retire(VkSwapchainKHR swapchain);

private:
    struct Registry {
        struct Device {
            bool enabled = false;
            LSFGVKMangoHudInfo lsfg{};
        };
        std::mutex m;
        std::unordered_map<VkInstance, LSFGVKMangoHudInfo> instances;
        std::unordered_map<VkDevice, Device> devices;
    };

    static Registry& registry() {
        static Registry value;
        return value;
    }
    Layer& layer;
    std::mutex m;
    std::mutex thread_m;
    std::thread thread;
    std::atomic<bool> stopping = false;
};
#endif
