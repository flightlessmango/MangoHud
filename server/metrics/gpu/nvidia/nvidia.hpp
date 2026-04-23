#pragma once

#include <cstdint>
#include <utility>
#include "gpu.hpp"
#include "nvml_loader.hpp"
#include "nvapi_loader.hpp"

class Nvidia : public GPU {
private:
    std::shared_ptr<libnvml_loader> nvml;
    nvmlDevice_t device = nullptr;
    bool init_nvml(const std::string& pci_dev);

    std::shared_ptr<libnvapi_loader> nvapi;
    uint32_t nvapi_device = 0;
    std::string nvapi_gpu_name;
    uint32_t nvapi_thermal_sensors_mask = 0;
    bool init_nvapi(const std::string& pci_dev);

    const std::vector<nvmlProcessInfo_v1_t> get_processes();
    bool get_thermal_sensors(libnvapi_loader::NvThermalSensors& sensors);

public:
    Nvidia(
        const std::string& drm_node, const std::string& pci_dev,
        uint16_t vendor_id, uint16_t device_id
    );

    bool nvml_available = false;
    bool nvapi_available = false;

    // System-related functions
    int     get_load()          override;

    float   get_vram_used()     override;

    float   get_memory_total()  override;
    int     get_memory_clock()  override;
    // in case of nvidia this is memory junction temperature
    // not memory chip temperature
    int     get_memory_temp()   override;

    int     get_temperature()   override;
    int     get_junction_temperature() override;

    int     get_core_clock()    override;
    int     get_voltage()       override;

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
