#pragma once

#include <cstdint>

#include "gpu.hpp"
#include "hwmon.hpp"
#include "fdinfo.hpp"
#include "gpu_metrics.hpp"

class AMDGPU : public GPU, private Hwmon, public FDInfo, private AMDGPUMetrics {
private:
    const std::vector<hwmon_sensor> sensors = {
        { "temperature"  ,  "temp1_input"     },
        { "junction_temp",  "temp2_input"     },
        { "memory_temp"  ,  "temp3_input"     },
        { "frequency"    ,  "freq1_input"     },
        { "memory_clock" ,  "freq2_input"     },
        { "voltage"      ,  "in0_input"       },
        { "average_power",  "power1_average"  },
        { "current_power",  "power1_input"    },
        { "power_limit"  ,  "power1_cap"      },
        { "fan"          ,  "fan1_input"      }
    };

    const std::vector<hwmon_sensor> sysfs_sensors = {
        { "load"        , "gpu_busy_percent"    },
        { "vram_used"   , "mem_info_vram_used"  },
        { "gtt_used"    , "mem_info_gtt_used"   },
        { "vram_total"  , "mem_info_vram_total" },
    };

    HwmonBase sysfs_hwmon;
    std::map<pid_t, uint64_t> previous_gpu_times;

    bool metrics_available = false;

public:
    AMDGPU(
        const std::string& drm_node, const std::string& pci_dev,
        uint16_t vendor_id, uint16_t device_id
    );

    void pre_poll_overrides()                   override;

    // System-related functions
    int     get_load()                          override;

    float   get_vram_used()                     override;
    float   get_gtt_used()                      override;
    float   get_memory_total()                  override;
    int     get_memory_clock()                  override;
    int     get_memory_temp()                   override;

    int     get_temperature()                   override;
    int     get_junction_temperature()          override;

    int     get_core_clock()                    override;
    int     get_voltage()                       override;

    float   get_power_usage()                   override;
    float   get_power_limit()                   override;

    bool    get_is_apu()                        override;
    float   get_apu_cpu_power()                 override;
    int     get_apu_cpu_temp()                  override;

    bool    get_is_power_throttled()            override;
    bool    get_is_current_throttled()          override;
    bool    get_is_temp_throttled()             override;
    bool    get_is_other_throttled()            override;

    int     get_fan_speed()                     override;

    // Process-related functions
    int     get_process_load(pid_t pid)         override;
    float   get_process_vram_used(pid_t pid)    override;
    float   get_process_gtt_used(pid_t pid)     override;
};
