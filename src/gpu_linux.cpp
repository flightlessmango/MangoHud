#include <algorithm>
#include <set>
#include "gpu.h"
#include "file_utils.h"

namespace fs = ghc::filesystem;

static std::string get_driver(const std::string& node) {
    std::error_code ec;
    std::string path = "/sys/class/drm/" + node + "/device/driver";

    if (!fs::exists(path, ec)) {
        SPDLOG_ERROR("{} doesn't exist", path);
        return "";
    }

    if (!fs::is_symlink(path, ec)) {
        SPDLOG_ERROR("{} is not a symlink (it should be)", path);
        return "";
    }

    std::string driver = fs::read_symlink(path, ec).string();
    if (ec)
        return "";
    driver = driver.substr(driver.rfind("/") + 1);

    return driver;
}

static std::string get_pci_device_address(const std::string& drm_card_path) {
    std::error_code ec;
    // /sys/class/drm/renderD128/device/subsystem -> /sys/bus/pci
    auto subsystem = fs::canonical(drm_card_path + "/device/subsystem", ec).string();
    if (ec)
        return "";
    auto idx = subsystem.rfind("/") + 1; // /sys/bus/pci
                                         //         ^
                                         //         |- find this guy
    if (subsystem.substr(idx) != "pci")
        return "";

    // /sys/class/drm/renderD128/device -> /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/0000:02:01.0/0000:03:00.0
    auto pci_addr = fs::read_symlink(drm_card_path + "/device", ec).string();
    if (ec)
        return "";
    idx = pci_addr.rfind("/") + 1; // /sys/.../0000:03:00.0
                                   //         ^
                                   //         |- find this guy

    return pci_addr.substr(idx); // 0000:03:00.0
}

std::vector<gpu_device> gpu_enumerate() {
    std::error_code ec;
    std::set<std::string> gpu_entries;

    std::error_code iterator_ec;
    for (fs::directory_iterator it("/sys/class/drm", iterator_ec), end;
         !iterator_ec && it != end; it.increment(iterator_ec)) {
        const auto& entry = *it;
        if (!entry.is_directory(ec))
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
    if (iterator_ec)
        SPDLOG_DEBUG("Failed to iterate {}: {}", fs::path("/sys/class/drm").string(), iterator_ec.message());

    std::vector<gpu_device> devices;
    for (const auto& node_name : gpu_entries) {
        gpu_device dev;
        dev.node = node_name;
        dev.driver = get_driver(node_name);

        if (dev.driver.empty()) {
            SPDLOG_DEBUG("Failed to query driver name of node \"{}\"", node_name);
            continue;
        }

        dev.pci_dev = get_pci_device_address("/sys/class/drm/" + node_name);

        if (!dev.pci_dev.empty())
        {
            try {
                dev.vendor_id = std::stoul(read_line("/sys/bus/pci/devices/" + dev.pci_dev + "/vendor"), nullptr, 16);
            } catch(...) {
                SPDLOG_ERROR("stoul failed on: {}", "/sys/bus/pci/devices/" + dev.pci_dev + "/vendor");
            }

            try {
                dev.device_id = std::stoul(read_line("/sys/bus/pci/devices/" + dev.pci_dev + "/device"), nullptr, 16);
            } catch (...) {
                SPDLOG_ERROR("stoul failed on: {}", "/sys/bus/pci/devices/" + dev.pci_dev + "/device");
            }
        }

        devices.push_back(std::move(dev));
    }
    return devices;
}
