#pragma once

#include <mutex>
#include <vector>
#include <memory>
#include <string>
#include "gpu.hpp"

class GPUS {
private:
    mutable std::mutex available_gpus_m;
    std::vector<std::shared_ptr<GPU>> available_gpus;

    std::string get_pci_device_address(const std::string& drm_card_path);
    std::string get_driver(const std::string& drm_card_path);

    const std::array<std::string, 8> supported_drivers = {
        "amdgpu", "nvidia", "i915", "xe", "panfrost", "msm_dpu", "msm_drm", "adreno"
    };

public:
    GPUS();
    std::vector<std::shared_ptr<GPU>> available() const;
};
