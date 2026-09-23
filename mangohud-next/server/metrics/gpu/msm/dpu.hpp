#pragma once

#include <cstdint>

#include "../gpu.hpp"
#include "../hwmon.hpp"
#include "fdinfo.hpp"

class MSM_DPU : public GPU, private Hwmon, public FDInfo {
private:
    const std::vector<hwmon_sensor> sensors = {
        { "temp", "temp1_input" }
    };

    std::map<pid_t, uint64_t> previous_gpu_times;
    std::ifstream junction_temp_file;
    std::ifstream memory_temp_file;
    std::ifstream core_clock_file;

    std::ifstream open_thermal_zone(const std::string& type);
    std::ifstream open_core_clock();
    int read_thermal_zone(std::ifstream& file);

protected:
    void pre_poll_overrides() override;

public:
    MSM_DPU(
        const std::string& drm_node, const std::string& pci_dev,
        uint16_t vendor_id, uint16_t device_id
    );

    // System-related functions
    int     get_temperature()                   override;
    int     get_junction_temperature()          override;
    int     get_memory_temp()                   override;
    int     get_core_clock()                    override;
    float   get_power_usage()                   override;

    // Process-related functions
    int     get_process_load(pid_t pid)         override;
    float   get_process_vram_used(pid_t pid)    override;
};
