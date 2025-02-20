#pragma once
#ifdef HAVE_FEX
#ifndef MANGOHUD_FEX_H
#define MANGOHUD_FEX_H
#include <cstdint>
#include <vector>
#include <string>

namespace fex {
bool is_fex_capable();
bool is_fex_pid_found();
const char* get_fex_app_type();

extern const char* fex_status;
extern std::string fex_version;

extern std::vector<float> fex_load_data;

struct fex_event_counts {
    public:
        void account(uint64_t total, std::chrono::time_point<std::chrono::steady_clock> now) {
            count = total;
            last_sample_count += total;

            const auto diff = now - last_chrono;
            if (diff >= std::chrono::seconds(1)) {
                // Calculate the average over the last second.
                const double NanosecondsInSeconds = 1'000'000'000.0;
                const auto diff_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count();
                const double Percentage = (double)diff_ns / NanosecondsInSeconds;
                average_sec = double(last_sample_count) * Percentage;
                last_sample_count = 0;
                last_chrono = now;
            }
        }

        void account_time(std::chrono::time_point<std::chrono::steady_clock> now) {
            last_chrono = now;
        }
        uint64_t Count() const { return count; }
        double Avg() const { return average_sec; }
    private:
        uint64_t count{};
        uint64_t last_sample_count{};
        double average_sec{};
        std::chrono::time_point<std::chrono::steady_clock> last_chrono{};
};
extern fex_event_counts sigbus_counts;
extern fex_event_counts smc_counts;
extern fex_event_counts softfloat_counts;

extern std::vector<float> fex_max_thread_loads;
void update_fex_stats();
}

#endif //MANGOHUD_FEX_H
#endif //HAVE_FEX
