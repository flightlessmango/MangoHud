#pragma once

#include <atomic>

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
