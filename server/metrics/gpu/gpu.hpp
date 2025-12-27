#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <regex>
#include <fstream>
#include <filesystem>
#include <set>

#include <pthread.h>
#include <spdlog/spdlog.h>

#include "gpu_metrics.hpp"

using namespace std::chrono_literals;
namespace fs = std::filesystem;

class GPU {
public:
    const std::string drm_node;
    const std::string pci_dev;
    const uint16_t vendor_id;
    const uint16_t device_id;

    // whether gpu is main one
    std::atomic<bool> is_active = false;
    std::atomic<bool> stop_thread = false;

    GPU(const std::string& drm_node, const std::string& pci_dev,
        uint16_t vendor_id, uint16_t device_id, const std::string& thread_name);

    ~GPU();

    void add_pid(pid_t pid);
    void print_metrics();
    void start_thread_worker();

    virtual gpu_metrics_system_t get_system_metrics();
    virtual std::map<pid_t, gpu_metrics_process_t> get_process_metrics();
    virtual gpu_metrics_process_t get_process_metrics(const size_t pid);

protected:
    gpu_metrics_system_t system_metrics = {};
    std::map<pid_t, gpu_metrics_process_t> process_metrics;

    std::mutex system_metrics_mutex, process_metrics_mutex;

    std::thread worker_thread;
    const std::string worker_thread_name;

    std::chrono::time_point<std::chrono::steady_clock> previous_time;
    std::chrono::nanoseconds delta_time_ns;

    virtual void pre_poll_overrides() {}
    void poll();
    void check_pids_existence();

    // System-related functions
    virtual int     get_load()                  { return -1; }

    virtual float   get_vram_used()             { return 0.f; }
    virtual float   get_gtt_used()              { return 0.f; }
    virtual float   get_memory_total()          { return 0.f; }
    virtual int     get_memory_clock()          { return 0; }
    virtual int     get_memory_temp()           { return 0; }

    virtual int     get_temperature()           { return 0; }
    virtual int     get_junction_temperature()  { return 0; }

    virtual int     get_core_clock()            { return 0; }
    virtual int     get_voltage()               { return 0; }

    virtual float   get_power_usage()           { return 0.f; }
    virtual float   get_power_limit()           { return 0.f; }

    virtual bool    get_is_apu()                { return false; }
    virtual float   get_apu_cpu_power()         { return 0.f;   }
    virtual int     get_apu_cpu_temp()          { return 0;     }

    virtual bool    get_is_power_throttled()    { return false; }
    virtual bool    get_is_current_throttled()  { return false; }
    virtual bool    get_is_temp_throttled()     { return false; }
    virtual bool    get_is_other_throttled()    { return false; }

    virtual int     get_fan_speed()             { return 0; }
    virtual bool    get_fan_rpm()               { return true; }

    // Process-related functions
    virtual int     get_process_load(pid_t pid)         { return 0; }
    virtual float   get_process_vram_used(pid_t pid)    { return 0.f; }
    virtual float   get_process_gtt_used(pid_t pid)     { return 0.f; }
};

class GPUS {
private:
    std::string get_pci_device_address(const std::string& drm_card_path);
    std::string get_driver(const std::string& drm_card_path);

    const std::array<std::string, 7> supported_drivers = {
        "amdgpu", "nvidia", "i915", "xe", "panfrost", "msm_dpu", "msm_drm"
    };

public:
    GPUS();
    std::vector<std::shared_ptr<GPU>> available_gpus;
};
