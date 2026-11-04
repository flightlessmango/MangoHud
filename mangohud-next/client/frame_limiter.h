#pragma once

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <vulkan/vulkan.h>
#include "mesa/os_time.h"

class frameLimiter {
    std::mutex acquire_m;
    int64_t last_acquire = 0;
    int64_t last_interval = 0;
    int64_t overhead = 0;

public:
    void limit(int64_t interval) {
        acquire(interval, UINT64_MAX, [](uint64_t) { return VK_SUCCESS; });
    }

    template<typename Acquire>
    VkResult acquire(int64_t interval, uint64_t timeout, Acquire&& driver) {
        if (interval <= 0 || timeout == 0)
            return driver(timeout);

        std::lock_guard lock(acquire_m);
        if (interval != last_interval) {
            overhead = 0;
            last_interval = interval;
        }
        const int64_t start = os_time_get_nano();
        auto remaining = [&] {
            if (timeout == UINT64_MAX)
                return timeout;
            const uint64_t elapsed = os_time_get_nano() - start;
            return elapsed < timeout ? timeout - elapsed : uint64_t{0};
        };
        const int64_t slack = last_acquire ? interval - (start - last_acquire) : 0;
        const int64_t delay = std::max<int64_t>(slack - overhead, 0);
        if (delay > 0) {
            const uint64_t budget = remaining();
            if (!budget)
                return VK_TIMEOUT;
            std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<uint64_t>(delay, budget)));
            if (static_cast<uint64_t>(delay) >= budget)
                return VK_TIMEOUT;
        }

        const uint64_t budget = remaining();
        if (!budget)
            return VK_TIMEOUT;
        const VkResult result = driver(budget);
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
            const int64_t end = os_time_get_nano();
            // Smooth sleep/acquire overhead by 1/16, capped at 5% or 100 us.
            if (slack > 0) {
                const int64_t max_correction = std::min<int64_t>(interval / 20, 100'000);
                const int64_t sample = std::clamp<int64_t>(end - start - delay, 0, max_correction);
                overhead += (sample - overhead) / 16;
            }
            last_acquire = end;
        }
        return result;
    }
};
