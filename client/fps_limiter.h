#pragma once
#include <mesa/os_time.h>

class fpsLimiter {
    private:
        int64_t target = 0;
        int64_t overhead = 0;
        size_t fps_limits_idx = 0;
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
        bool use_early;
        bool active = false;

        fpsLimiter(bool use_early) : use_early(use_early) {
            auto& fps_limit = get_params()->fps_limit;
            if (fps_limit.empty())
                return;

            active = true;

            float tar = fps_limit[fps_limits_idx];
            target = tar <= 0.0f ? 0 : int64_t(1'000'000'000.0f / tar);
        }

        void limit(bool is_early) {
            if (!active || target <= 0)
                return;

            frame_start = os_time_get_nano();;

            if (is_early != use_early) return;

            int64_t sleep_time = calc_sleep(frame_start, frame_end);
            if (sleep_time > 0)
                do_sleep(sleep_time);

            frame_end = os_time_get_nano();
        }

        void next_limit() {
            auto& v = get_params()->fps_limit;
            if (v.empty())
                return;

            fps_limits_idx = (fps_limits_idx + 1) % v.size();
            auto next_target = v[fps_limits_idx];
            target = next_target <= 0.0f ? 0 : int64_t(1'000'000'000.0f / next_target);
            SPDLOG_DEBUG("Changed fps limit to {}", next_target);
        }

        int current_limit() {
            auto& v = get_params()->fps_limit;
            return v[fps_limits_idx];
        }
};

extern std::shared_ptr<fpsLimiter> fps_limiter;
