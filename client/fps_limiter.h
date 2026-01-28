#pragma once
#include <mesa/os_time.h>
#include <cstddef>
#include <thread>
#include <memory>
#include <vulkan/vulkan.h>
#include <deque>
#include <vector>
#include <atomic>
#include <unordered_set>
#include "vkroots.h"

class queueLimiter {
public:
    uint32_t max_in_flight = 0;
    std::atomic<uint64_t> waits{0};
    std::atomic<uint64_t> waited_ns{0};
    std::atomic<uint64_t> max_depth_seen{0};
    std::deque<VkFence> in_flight;
    std::unordered_set<VkQueue> present_queues;
    std::mutex present_queues_mtx;

    void throttle_before_submit(const vkroots::VkDeviceDispatch* d) {
        if (max_in_flight == 0)
            return;

        auto depth = (uint64_t)in_flight.size();
        uint64_t prev_max = max_depth_seen.load(std::memory_order_relaxed);
        while (depth > prev_max && !max_depth_seen.compare_exchange_weak(prev_max, depth));

        while (in_flight.size() >= max_in_flight) {
            VkFence oldest = in_flight.front();
            if (oldest == VK_NULL_HANDLE) {
                in_flight.pop_front();
                continue;
            }

            if (d->GetFenceStatus(d->Device, oldest) == VK_NOT_READY) {
                int64_t t0 = os_time_get_nano();
                d->WaitForFences(d->Device, 1, &oldest, VK_TRUE, UINT64_MAX);
                int64_t t1 = os_time_get_nano();

                waits.fetch_add(1, std::memory_order_relaxed);
                waited_ns.fetch_add((uint64_t)(t1 - t0), std::memory_order_relaxed);
            }

            d->ResetFences(d->Device, 1, &oldest);
            in_flight.pop_front();
        }
    }

    VkResult mark_after_submit(const vkroots::VkDeviceDispatch* d, VkQueue queue) {
        if (max_in_flight == 0)
            return VK_SUCCESS;

        VkFence f = get_fence(d);
        if (f == VK_NULL_HANDLE)
            return VK_SUCCESS;

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkResult r = d->QueueSubmit(queue, 1, &si, f);
        if (r == VK_SUCCESS) {
            in_flight.push_back(f);

            auto depth = (uint64_t)in_flight.size();
            uint64_t prev_max = max_depth_seen.load(std::memory_order_relaxed);
            while (depth > prev_max && !max_depth_seen.compare_exchange_weak(prev_max, depth));
        }
        return r;
    }

    bool is_present_queue(VkQueue queue) {
        std::lock_guard lock(present_queues_mtx);
        return (present_queues.find(queue) != present_queues.end());
    }

private:
    std::vector<VkFence> pool;
    size_t pool_cursor = 0;

    VkFence get_fence(const vkroots::VkDeviceDispatch* d) {
        if (pool.empty()) {
            pool.resize(8, VK_NULL_HANDLE);

            VkFenceCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

            for (auto& f : pool) {
                if (d->CreateFence(d->Device, &ci, nullptr, &f) != VK_SUCCESS)
                    f = VK_NULL_HANDLE;
            }
        }

        VkFence f = pool[pool_cursor++ % pool.size()];
        return f;
    }
};

struct PresentState {
    uint64_t next_id = 0;
    uint64_t last_assigned = 0;
    uint64_t last_queued = 0;
    uint64_t last_completed = 0;
};

class presentLimiter {
public:
    PFN_vkWaitForPresentKHR WaitForPresentKHR = nullptr;

    presentLimiter(PFN_vkWaitForPresentKHR wait) : WaitForPresentKHR(wait) {}

    void on_present(const VkPresentInfoKHR* pPresentInfo, uint64_t* out_ids) {
        std::lock_guard<std::mutex> lock(mtx);
        for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
            VkSwapchainKHR sc = pPresentInfo->pSwapchains[i];
            auto& st = states[sc];
            uint64_t id = ++st.next_id;
            st.last_assigned = id;
            out_ids[i] = id;
        }
    }

    void on_present_result(const VkPresentInfoKHR* pi, const uint64_t* ids, VkResult r) {
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            return;
        }

        std::lock_guard<std::mutex> lock(mtx);
        for (uint32_t i = 0; i < pi->swapchainCount; i++) {
            VkSwapchainKHR sc = pi->pSwapchains[i];
            auto& st = states[sc];

            uint64_t id = ids ? ids[i] : 0;
            if (id > 0) {
                st.last_queued = std::max(st.last_queued, id);
                st.next_id = std::max(st.next_id, id);
            }
        }
    }

    void throttle(VkDevice device, VkSwapchainKHR swapchain, uint64_t allowed_ahead) {
        if (!WaitForPresentKHR) return;
        if (swapchain == VK_NULL_HANDLE) return;

        uint64_t queued = 0;
        uint64_t completed = 0;
        uint64_t depth = 0;

        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = states.find(swapchain);
            if (it == states.end()) return;

            auto& st = it->second;
            queued = st.last_queued;
            completed = st.last_completed;
            depth = queued - completed;
        }

        if (depth <= allowed_ahead) return;

        uint64_t wait_id = queued - allowed_ahead;
        if (wait_id <= completed) return;

        VkResult r = WaitForPresentKHR(device, swapchain, wait_id, 0);
        if (r == VK_TIMEOUT) {
            r = WaitForPresentKHR(device, swapchain, wait_id, 2'000'000ull);
        }

        if (r != VK_SUCCESS) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = states.find(swapchain);
            if (it == states.end()) return;

            auto& st = it->second;
            if (wait_id > st.last_completed)
                st.last_completed = wait_id;
        }
    }

private:
    std::mutex mtx;
    std::unordered_map<VkSwapchainKHR, PresentState> states;
};

extern std::unique_ptr<presentLimiter> present_limiter;

class fpsLimiter {
    private:
        int64_t target = 0;
        int64_t overhead = 0;
        int64_t frame_start = 0;
        int64_t frame_end = 0;

        int64_t calc_sleep(int64_t start, int64_t end) {
            if (target <= 0 || start <= 0)
                return 0;

            int64_t work = start - end;
            if (work < 0)
                work = 0;

            int64_t sleep = (target - work) - overhead;
            return sleep > 0 ? sleep : 0;
        }

        void do_sleep(int64_t sleep_time) {
            if (sleep_time <= 0)
                return;

            int64_t t0 = os_time_get_nano();

            std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_time));

            int64_t over = (os_time_get_nano() - t0) - sleep_time;
            if (over < 0 || over > (target / 2))
                over = 0;

            overhead = over;
        }

    public:
        std::unique_ptr<queueLimiter> q_limiter;
        bool use_early;
        bool active = false;

        fpsLimiter(bool use_early) : use_early(use_early) {
            q_limiter = std::make_unique<queueLimiter>();
            set_fps_limit(0);
        }

        void set_fps_limit(float fps) {
            const int64_t new_target = (fps <= 0.0f) ? 0 : static_cast<int64_t>(1'000'000'000.0f / fps);

            if (target == new_target) {
                return;
            }

            target = new_target;
            active = (new_target > 0);
            q_limiter->max_in_flight = active ? 1 : 0;
        }

        void limit(bool is_early) {
            if (!active || target <= 0)
                return;

            if (is_early != use_early) return;

            frame_start = os_time_get_nano();
            int64_t sleep_time = calc_sleep(frame_start, frame_end);
            if (sleep_time > 0)
                do_sleep(sleep_time);

            frame_end = os_time_get_nano();
        }
};

extern std::unique_ptr<fpsLimiter> fps_limiter;
