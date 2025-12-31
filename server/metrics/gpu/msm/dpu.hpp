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

protected:
    void pre_poll_overrides() override;

public:
    MSM_DPU(
        const std::string& drm_node, const std::string& pci_dev,
        uint16_t vendor_id, uint16_t device_id
    );

    // System-related functions
    int     get_temperature()                   override;

    // Process-related functions
    int     get_process_load(pid_t pid)         override;
};
