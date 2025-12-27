#pragma once

#include <cstdint>

#include "gpu.hpp"
#include "hwmon.hpp"
#include "fdinfo.hpp"
#include "xe_drm.hpp"

class Intel_xe : public GPU, private Hwmon, public FDInfo, private xe_drm {
private:
    enum GPU_throttle_status : int {
        POWER   = 0b0001,
        CURRENT = 0b0010,
        TEMP    = 0b0100,
        OTHER   = 0b1000,
    };

    const std::vector<hwmon_sensor> sensors = {
        { "voltage"     , "in1_input"       },
        // technically, there are 3 fan sensors, but just pick first one
        { "fan_speed"   , "fan1_input"      },
        { "temp"        , "temp2_input"     },
        { "vram_temp"   , "temp3_input"     },
        { "energy"      , "energy2_input"   },
        { "power_limit" , "power2_max"      }
    };

    uint64_t previous_power_usage = 0;
    std::map<std::string, std::pair<uint64_t, uint64_t>> previous_cycles;

    void find_gt_dir();
    void load_throttle_reasons(
        std::string throttle_folder, std::vector<std::string> throttle_reasons,
        std::vector<std::ifstream>& ifs_throttle_reason
    );
    bool check_throttle_reasons(std::vector<std::ifstream>& ifs_throttle_reason);
    int get_throttling_status();

    std::ifstream ifs_gpu_clock;
    std::ifstream ifs_throttle_status;

    std::vector<std::ifstream> ifs_throttle_power;
    std::vector<std::ifstream> ifs_throttle_current;
    std::vector<std::ifstream> ifs_throttle_temp;

    int throttling = 0;

    std::vector<std::string> throttle_power   = { "reason_pl1", "reason_pl2"    };
    std::vector<std::string> throttle_current = { "reason_pl4", "reason_vr_tdc" };
    std::vector<std::string> throttle_temp = {
        "reason_prochot", "reason_ratl",
        "reason_thermal", "reason_vr_thermalert"
    };

    bool drm_available = false;

protected:
    void pre_poll_overrides() override;

public:
    Intel_xe(
        const std::string& drm_node, const std::string& pci_dev,
        uint16_t vendor_id, uint16_t device_id
    );

    // System-related functions
    // int     get_load() override;             // Not available

    float   get_vram_used()                     override;
    // float   get_gtt_used()         override; // Investigate
    float   get_memory_total()                  override;
    int     get_memory_temp()                   override;

    int     get_temperature()                   override;
    // int get_junction_temperature() override; // Not available

    int     get_core_clock()                    override;
    int     get_voltage()                       override;

    float   get_power_usage()                   override;
    float   get_power_limit()                   override;

    // float   get_apu_cpu_power() override;    // Investigate
    // int     get_apu_cpu_temp() override;     // Investigate

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
