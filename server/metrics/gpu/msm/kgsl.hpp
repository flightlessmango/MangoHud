#pragma once

#include <cstdint>

#include "../gpu.hpp"
#include "../hwmon.hpp"

class MSM_KGSL : public GPU, private Hwmon {
private:
    const std::vector<hwmon_sensor> sensors = {
        { "load"        ,  "gpu_busy_percentage"    },
        { "temp"        ,  "temp"                   },
        { "core_clock"  ,  "clock_mhz"              }
    };

protected:
    void pre_poll_overrides() override;

public:
    MSM_KGSL(
        const std::string& drm_node, const std::string& pci_dev,
        uint16_t vendor_id, uint16_t device_id
    );

    // System-related functions
    int     get_load()          override;
    int     get_temperature()   override;
    int     get_core_clock()    override;
};
