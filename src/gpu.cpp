#include "gpu.h"
#include "file_utils.h"

namespace fs = ghc::filesystem;

GPUS::GPUS(overlay_params* const* params_pointer) : params_pointer(params_pointer) {
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
                gpu_entries.insert(node_name);  // Store the render entry
            }
        }
    }

    // Now process the sorted GPU entries
    uint8_t idx = 0, total_active = 0;

    for (const auto& node_name : gpu_entries) {
        const std::string driver = get_driver(node_name);

        if (driver.empty()) {
            SPDLOG_DEBUG("Failed to query driver name of node \"{}\"", node_name);
            continue;
        }

        {
            const std::string* d =
                std::find(std::begin(supported_drivers), std::end(supported_drivers), driver);

            if (d == std::end(supported_drivers)) {
                SPDLOG_WARN(
                    "node \"{}\" is using driver \"{}\" which is unsupported by MangoHud. Skipping...",
                    node_name, driver
                );
                continue;
            }
        }

        std::string path = "/sys/class/drm/" + node_name;
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

        std::shared_ptr<GPU> ptr =
            std::make_shared<GPU>(node_name, vendor_id, device_id, pci_dev, driver);

        if (params()->gpu_list.size() == 1 && params()->gpu_list[0] == idx++)
            ptr->is_active = true;

        if (!params()->pci_dev.empty() && pci_dev == params()->pci_dev)
            ptr->is_active = true;

        available_gpus.emplace_back(ptr);

        SPDLOG_DEBUG(
            "GPU Found: node_name: {}, driver: {}, vendor_id: {:x} device_id: {:x} pci_dev: {}",
            node_name, driver, vendor_id, device_id, pci_dev
        );

        if (ptr->is_active) {
            SPDLOG_INFO(
                "Set {} as active GPU (driver={} id={:x}:{:x} pci_dev={})",
                node_name, driver, vendor_id, device_id, pci_dev
            );
            total_active++;
        }
    }

    if (total_active < 2)
        return;

    for (auto& gpu : available_gpus) {
        if (!gpu->is_active)
            continue;

        SPDLOG_WARN(
            "You have more than 1 active GPU, check if you use both pci_dev "
            "and gpu_list. If you use fps logging, MangoHud will log only "
            "this GPU: name = {}, driver = {}, vendor = {:x}, pci_dev = {}",
            gpu->drm_node, gpu->driver, gpu->vendor_id, gpu->pci_dev
        );

        break;
    }

}

std::string GPUS::get_driver(const std::string& node) {
    std::string path = "/sys/class/drm/" + node + "/device/driver";

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

std::string GPUS::get_pci_device_address(const std::string& drm_card_path) {
    // /sys/class/drm/renderD128/device/subsystem -> /sys/bus/pci
    auto subsystem = fs::canonical(drm_card_path + "/device/subsystem").string();
    auto idx = subsystem.rfind("/") + 1; // /sys/bus/pci
                                         //         ^
                                         //         |- find this guy
    if (subsystem.substr(idx) != "pci")
        return "";

    // /sys/class/drm/renderD128/device -> /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/0000:02:01.0/0000:03:00.0
    auto pci_addr = fs::read_symlink(drm_card_path + "/device").string();
    idx = pci_addr.rfind("/") + 1; // /sys/.../0000:03:00.0
                                   //         ^
                                   //         |- find this guy

    return pci_addr.substr(idx); // 0000:03:00.0
}

int GPU::index_in_selected_gpus() {
    auto selected_gpus = gpus->selected_gpus();
    auto it = std::find_if(selected_gpus.begin(), selected_gpus.end(),
                        [this](const std::shared_ptr<GPU>& gpu) {
                            return gpu.get() == this;
                        });
    if (it != selected_gpus.end()) {
        return std::distance(selected_gpus.begin(), it);
    }
    return -1;
}

std::string GPU::gpu_text() {
    std::string gpu_text;
    size_t index = this->index_in_selected_gpus();

    if (gpus->selected_gpus().size() == 1) {
        // When there's exactly one selected GPU, return "GPU" without index
        gpu_text = "GPU";
        if (gpus->params()->gpu_text.size() > 0) {
            gpu_text = gpus->params()->gpu_text[0];
        }
    } else if (gpus->selected_gpus().size() > 1) {
        // When there are multiple selected GPUs, use GPU+index or matching gpu_text
        gpu_text = "GPU" + std::to_string(index);
        if (gpus->params()->gpu_text.size() > index) {
            gpu_text = gpus->params()->gpu_text[index];
        }
    } else {
        // Default case for no selected GPUs
        gpu_text = "GPU";
    }

    return gpu_text;
}

std::string GPU::vram_text() {
    std::string vram_text;
    size_t index = this->index_in_selected_gpus();
    if (gpus->selected_gpus().size() > 1)
        vram_text = "VRAM" + std::to_string(index);
    else
        vram_text = "VRAM";
    return vram_text;
}

std::unique_ptr<GPUS> gpus = nullptr;
