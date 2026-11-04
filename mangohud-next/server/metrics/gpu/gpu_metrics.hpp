#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>

struct gpu_metrics_process_t {
    int     load;
    float   vram_used;
    float   gtt_used;
};

struct gpu_metrics_system_t {
    int     load;

    float   vram_used;
    float   gtt_used;
    float   memory_total;
    int     memory_clock;
    int     memory_temp;

    int     temperature;
    int     junction_temperature;

    int     core_clock;
    int     voltage;

    float   power_usage;
    float   power_limit;

    bool    is_apu;
    float   apu_cpu_power;
    int     apu_cpu_temp;

    bool    is_power_throttled;
    bool    is_current_throttled;
    bool    is_temp_throttled;
    bool    is_other_throttled;

    int     fan_speed;
    bool    fan_rpm;
};

constexpr std::chrono::milliseconds gpu_metrics_update_period{500};
constexpr std::chrono::milliseconds gpu_metrics_polling_period{25};
constexpr size_t gpu_metrics_sample_count = gpu_metrics_update_period / gpu_metrics_polling_period;

using SystemMetricsBuffer = std::array<gpu_metrics_system_t, gpu_metrics_sample_count>;
using ProcessMetricsBuffer = std::array<gpu_metrics_process_t, gpu_metrics_sample_count>;

#define AVERAGE(FIELD, T)                                               \
    do {                                                                \
        T value_sum = {};                                               \
        for (size_t s = 0; s < samples; s++)                            \
            value_sum += metrics_buffer[s].FIELD;                       \
        metrics.FIELD = value_sum / static_cast<T>(samples);            \
    } while(0)

#define MAX(FIELD)                                                       \
    do {                                                                \
        auto value = metrics_buffer[0].FIELD;                           \
        for (size_t s = 1; s < samples; s++)                            \
            value = value > metrics_buffer[s].FIELD ? value : metrics_buffer[s].FIELD; \
        metrics.FIELD = value;                                          \
    } while(0)

inline gpu_metrics_system_t average_system_metrics(const SystemMetricsBuffer& metrics_buffer, size_t samples) {
    gpu_metrics_system_t metrics = {};

    AVERAGE(load, int);
    MAX(vram_used);
    MAX(gtt_used);
    AVERAGE(memory_total, float);
    AVERAGE(memory_clock, int);
    AVERAGE(memory_temp, int);
    AVERAGE(temperature, int);
    AVERAGE(junction_temperature, int);
    AVERAGE(core_clock, int);
    AVERAGE(voltage, int);
    AVERAGE(power_usage, float);
    AVERAGE(power_limit, float);
    MAX(is_apu);
    AVERAGE(apu_cpu_power, float);
    AVERAGE(apu_cpu_temp, int);
    MAX(is_power_throttled);
    MAX(is_current_throttled);
    MAX(is_temp_throttled);
    MAX(is_other_throttled);
    MAX(fan_speed);
    MAX(fan_rpm);

    return metrics;
}

inline gpu_metrics_process_t average_process_metrics(const ProcessMetricsBuffer& metrics_buffer, size_t samples) {
    gpu_metrics_process_t metrics = {};

    AVERAGE(load, int);
    MAX(vram_used);
    MAX(gtt_used);

    return metrics;
}

struct gpu_t {
    bool is_active;

    gpu_metrics_process_t process_metrics;
    gpu_metrics_system_t system_metrics;
};

struct memory_t {
    float used      = 0;
    float total     = 0;
    float swap_used = 0;

    float process_resident  = 0;
    float process_shared    = 0;
    float process_virtual   = 0;
};

struct io_stats_t {
    float read_mb_per_sec = 0.f;
    float write_mb_per_sec = 0.f;
};

typedef struct core_info_t {
   int load       = 0;
   int frequency  = 0;
   int temp       = 0;
   float power    = 0.f;
} cpu_info_t;

struct mangohud_message {
    uint8_t num_of_gpus;
    gpu_t gpus[8];

    memory_t memory;
    io_stats_t io_stats;

    cpu_info_t cpu;
    uint16_t num_of_cores;
    core_info_t cores[1024];
};

struct process_metrics {
    gpu_metrics_process_t gpus[8];
    struct {
        float resident = 0;
        float shared = 0;
        float virt = 0;
    } memory;
    io_stats_t io_stats;
};

struct metrics {
    cpu_info_t cpu;
    uint16_t num_of_cores;
    core_info_t cores[1024];

    uint8_t num_of_gpus;
    gpu_metrics_system_t gpus[8];

    struct {
        float used = 0;
        float total = 0;
        float swap_used = 0;
    } memory;

    std::unordered_map<pid_t, process_metrics> pids;
};

extern std::mutex current_metrics_lock;
extern metrics current_metrics;
extern std::atomic<bool> should_exit;
