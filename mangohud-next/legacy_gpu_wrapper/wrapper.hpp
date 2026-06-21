#pragma once

#include <unistd.h>
#include <cstdint>
#include <string>
#include <memory>

class LegacyGPUWrapper {
public:
    LegacyGPUWrapper(
        const std::string& driver, const std::string& drm_node, const std::string& pci_dev,
        uint16_t vendor_id, uint16_t device_id
    );
    
    ~LegacyGPUWrapper();

    void poll();
    void add_pid(pid_t pid);

    // System-related functions
    int     get_load();

    float   get_vram_used();
    float   get_gtt_used();
    float   get_memory_total();
    int     get_memory_clock();
    int     get_memory_temp();

    int     get_temperature();
    int     get_junction_temperature();

    int     get_core_clock();
    int     get_voltage();

    float   get_power_usage();
    float   get_power_limit();

    bool    get_is_apu();
    float   get_apu_cpu_power();
    int     get_apu_cpu_temp();

    bool    get_is_power_throttled();
    bool    get_is_current_throttled();
    bool    get_is_temp_throttled();
    bool    get_is_other_throttled();

    int     get_fan_speed();
    bool    get_fan_rpm();

    // Process-related functions
    int     get_process_load(pid_t pid);
    float   get_process_vram_used(pid_t pid);
    float   get_process_gtt_used(pid_t pid);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};


class LegacyFDInfoWrapper {
public:
    LegacyFDInfoWrapper(const std::string& drm_node);
    ~LegacyFDInfoWrapper();

    void add_pid(pid_t pid);
    void poll_all();
    float get_memory_used(pid_t pid, const std::string& key);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
