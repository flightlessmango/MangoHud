#include "intel/i915/i915.hpp"
#include "intel/xe/xe.hpp"
#include "amdgpu/amdgpu.hpp"
#include "nvidia/nvidia.hpp"
#include "panfrost.hpp"
#include "panthor.hpp"
#include "msm/dpu.hpp"
#include "msm/kgsl.hpp"
#include "../common/helpers.hpp"

#include "gpus.hpp"

GPUS::GPUS() {
    std::set<std::string> gpu_entries;

    for (const auto& entry : fs::directory_iterator("/sys/class/drm")) {
        if (!entry.is_directory())
            continue;

        std::string node_name = entry.path().filename().string();

        // Check if the directory is a render node (e.g., renderD128, renderD129, etc.)
        if (node_name.find("renderD") == 0 && node_name.length() > 7) {
            // Ensure the rest of the string after "renderD" is numeric
            std::string render_number = node_name.substr(7);
            if (std::all_of(render_number.begin(), render_number.end(), ::isdigit)) {
                gpu_entries.emplace(node_name); // Store the render entry
            }
        }
    }

    // Now process the sorted GPU entries
    uint8_t /*idx = 0,*/ total_active = 0;

    for (const auto& drm_node : gpu_entries) {
        const std::string path = "/sys/class/drm/" + drm_node;
        const std::string driver = get_driver(path);

         {
            const std::string* d =
                std::find(std::begin(supported_drivers), std::end(supported_drivers), driver);

            if (d == std::end(supported_drivers)) {
                SPDLOG_WARN(
                    "node \"{}\" is using driver \"{}\" which is unsupported by MangoHud. Skipping...",
                    drm_node, driver
                );
                continue;
            }
        }

        std::string device_address = get_pci_device_address(path);  // Store the result
        const char* pci_dev = device_address.c_str();

        uint32_t vendor_id = 0;
        uint32_t device_id = 0;

        if (!device_address.empty())
        {
            try {
                vendor_id = std::stoul(read_line("/sys/bus/pci/devices/" + device_address + "/vendor"), nullptr, 16);
            } catch(...) {
                SPDLOG_ERROR("stoul failed on: {}", "/sys/bus/pci/devices/" + device_address + "/vendor");
            }

            try {
                device_id = std::stoul(read_line("/sys/bus/pci/devices/" + device_address + "/device"), nullptr, 16);
            } catch (...) {
                SPDLOG_ERROR("stoul failed on: {}", "/sys/bus/pci/devices/" + device_address + "/device");
            }
        }

        std::shared_ptr<GPU> gpu;

        if (driver == "i915") {
            gpu = std::make_shared<Intel_i915>(drm_node, pci_dev, vendor_id, device_id);
        } else if (driver == "xe") {
            gpu = std::make_shared<Intel_xe>(drm_node, pci_dev, vendor_id, device_id);
        } else if (driver == "amdgpu") {
            gpu = std::make_shared<AMDGPU>(drm_node, pci_dev, vendor_id, device_id);
        } else if (driver == "nvidia") {
            gpu = std::make_shared<Nvidia>(drm_node, pci_dev, vendor_id, device_id);

            if (Nvidia* ptr = dynamic_cast<Nvidia*>(gpu.get())) {
                if (!ptr->nvml_available) {
                    SPDLOG_WARN(
                        "NVML is not loaded. Nvidia metrics are not available!. "
                        "Skipping node {}.", drm_node
                    );

                    continue;
                }
            }
        } else if (driver == "panfrost") {
            gpu = std::make_shared<Panfrost>(drm_node, pci_dev, vendor_id, device_id);
        } else if (driver == "panthor") {
            gpu = std::make_shared<Panthor>(drm_node, pci_dev, vendor_id, device_id);
        } else if (driver == "msm_dpu") {
            gpu = std::make_shared<MSM_DPU>(drm_node, pci_dev, vendor_id, device_id);
        } else if (driver == "msm_drm") {
            gpu = std::make_shared<MSM_KGSL>(drm_node, pci_dev, vendor_id, device_id);
        } else {
            continue;
        }

        {
            std::lock_guard lock(available_gpus_m);
            available_gpus.push_back(gpu);
        }
        // if (params->gpu_list.size() == 1 && params->gpu_list[0] == idx++)
        //     gpu->is_active = true;

        // if (!params->pci_dev.empty() && pci_dev == params->pci_dev)
        //     gpu->is_active = true;

        SPDLOG_DEBUG("GPU Found: drm_node: {}, driver: {}, vendor_id: {:x} device_id: {:x} pci_dev: {}", drm_node, driver, vendor_id, device_id, pci_dev);

        if (gpu->is_active) {
            SPDLOG_INFO("Set {} as active GPU (driver={} id={:x}:{:x} pci_dev={})", drm_node, driver, vendor_id, device_id, pci_dev);
            total_active++;
        }
    }

    if (total_active < 2)
        return;

    std::lock_guard lock(available_gpus_m);
    for (auto& gpu : available_gpus) {
        if (!gpu->is_active)
            continue;

        SPDLOG_WARN(
            "You have more than 1 active GPU, check if you use both pci_dev "
            "and gpu_list. If you use fps logging, MangoHud will log only "
            "this GPU: name = {}, vendor = {:x}, pci_dev = {}",
            gpu->drm_node, gpu->vendor_id, gpu->pci_dev
        );

        break;
    }
}

std::vector<std::shared_ptr<GPU>> GPUS::available() const {
    std::lock_guard lock(available_gpus_m);
    return available_gpus;
}

std::string GPUS::get_pci_device_address(const std::string& drm_card_path) {
    // /sys/class/drm/renderD128/device/subsystem -> /sys/bus/pci
    auto subsystem = fs::canonical(drm_card_path + "/device/subsystem").string();
    auto idx = subsystem.rfind("/") + 1; // /sys/bus/pci
                                         //         ^
                                         //         |- find this guy
    if (subsystem.substr(idx) != "pci")
        return "";

    // /sys/class/drm/renderD128/device
    //           convert to
    // /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/0000:02:01.0/0000:03:00.0
    auto pci_addr = fs::read_symlink(drm_card_path + "/device").string();
    idx = pci_addr.rfind("/") + 1; // /sys/.../0000:03:00.0
                                   //         ^
                                   //         |- find this guy

    return pci_addr.substr(idx); // 0000:03:00.0
}

std::string GPUS::get_driver(const std::string& drm_card_path) {
    std::string path = drm_card_path + "/device/driver";

    if (!fs::exists(path)) {
        SPDLOG_ERROR("{} doesn't exist", path);
        return "";
    }

    if (!fs::is_symlink(path)) {
        SPDLOG_ERROR("{} is not a symlink (it should be)", path);
        return "";
    }

    std::string driver = fs::read_symlink(path).string();
    driver = driver.substr(driver.rfind("/") + 1);

    return driver;
}
