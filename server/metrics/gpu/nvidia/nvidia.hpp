#pragma once

#include <cstdint>
#include <utility>
#include "gpu.hpp"
#include "nvml_loader.hpp"

class Nvidia : public GPU {
private:
    std::shared_ptr<libnvml_loader> nvml;
    nvmlDevice_t device = nullptr;
    bool init_nvml(const std::string& pci_dev);

    const std::vector<nvmlProcessInfo_v1_t> get_processes();

public:
    Nvidia(
        const std::string& drm_node, const std::string& pci_dev,
        uint16_t vendor_id, uint16_t device_id
    );

    bool nvml_available = false;

    // System-related functions
    int     get_load()          override;

    float   get_vram_used()     override;

    float   get_memory_total()  override;
    int     get_memory_clock()  override;

    int     get_temperature()   override;

    int     get_core_clock()    override;

    float   get_power_usage()   override;
    float   get_power_limit()   override;

    bool    get_is_power_throttled()    override;
    bool    get_is_temp_throttled()     override;
    bool    get_is_other_throttled()    override;

    int     get_fan_speed()     override;
    bool    get_fan_rpm()       override;

    // Process-related functions
    // int     get_process_load(pid_t pid)         { return 0; }
    float   get_process_vram_used(pid_t pid) override;
};
