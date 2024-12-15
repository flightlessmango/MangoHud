#include "gpu.h"
#include <cstdint>
#include <inttypes.h>
#include <memory>
#include <functional>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <fstream>
#include <spdlog/spdlog.h>
#include "timing.hpp"
#include "amdgpu.h"

#include "file_utils.h"
using namespace std::chrono_literals;

#include <iostream>
#include <filesystem>
#include <string>
namespace fs = ghc::filesystem;

GPUS::GPUS(overlay_params* params) : params(params) {
    std::vector<std::string> gpu_entries;

    for (const auto& entry : fs::directory_iterator("/sys/class/drm")) {
        if (!entry.is_directory())
            continue;

        std::string node_name = entry.path().filename().string();

        // Check if the directory is a render node (e.g., renderD128, renderD129, etc.)
        if (node_name.find("renderD") == 0 && node_name.length() > 7) {
            // Ensure the rest of the string after "renderD" is numeric
            std::string render_number = node_name.substr(7);
            if (std::all_of(render_number.begin(), render_number.end(), ::isdigit)) {
                gpu_entries.push_back(node_name);  // Store the render entry
            }
        }
    }

    // Sort the entries based on the numeric value of the render number
    std::sort(gpu_entries.begin(), gpu_entries.end(), [](const std::string& a, const std::string& b) {
        int num_a = std::stoi(a.substr(7));
        int num_b = std::stoi(b.substr(7));
        return num_a < num_b;
    });

    // Now process the sorted GPU entries
    uint8_t idx = 0, total_active = 0;

    for (const auto& node_name : gpu_entries) {
        std::string path = "/sys/class/drm/" + node_name;
        std::string device_address = get_pci_device_address(path);  // Store the result
        const char* pci_dev = device_address.c_str();

        uint32_t vendor_id = 0;
        uint32_t device_id = 0;
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

        std::shared_ptr<GPU> ptr = std::make_shared<GPU>(node_name, vendor_id, device_id, pci_dev);

        if (params->gpu_list.size() == 1 && params->gpu_list[0] == idx++)
            ptr->is_active = true;

        if (!params->pci_dev.empty() && pci_dev == params->pci_dev)
            ptr->is_active = true;

        available_gpus.emplace_back(ptr);

        SPDLOG_DEBUG("GPU Found: node_name: {}, vendor_id: {:x} device_id: {:x} pci_dev: {}", node_name, vendor_id, device_id, pci_dev);

        if (ptr->is_active) {
            SPDLOG_INFO("Set {} as active GPU (id={:x}:{:x} pci_dev={})", node_name, vendor_id, device_id, pci_dev);
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
            "this GPU: name = {}, vendor = {:x}, pci_dev = {}",
            gpu->name, gpu->vendor_id, gpu->pci_dev
        );

        break;
    }

}

std::string GPU::is_i915_or_xe() {
    std::string path = "/sys/bus/pci/devices/";
    path += pci_dev + "/driver";

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
    // Resolve the symbolic link to get the actual device path
    fs::path device_path = fs::canonical(fs::path(drm_card_path) / "device");

    // Convert the resolved device path to a string
    std::string path_str = device_path.string();

    // Extract the last PCI address from the path using a regular expression
    // This regex matches typical PCI addresses like 0000:03:00.0
    std::regex pci_address_regex(R"((\d{4}:[a-z0-9]{2}:\d{2}\.\d))");
    std::smatch match;
    std::string pci_address;

    // Search for all matches and store the last one
    auto it = std::sregex_iterator(path_str.begin(), path_str.end(), pci_address_regex);
    auto end = std::sregex_iterator();
    for (std::sregex_iterator i = it; i != end; ++i) {
        pci_address = (*i).str();
    }

    if (!pci_address.empty()) {
        return pci_address;  // Return the last matched PCI address
    } else {
        SPDLOG_DEBUG("PCI address not found in the path: " + path_str);
        return "";
    }
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
        if (gpus->params->gpu_text.size() > 0) {
            gpu_text = gpus->params->gpu_text[0];
        }
    } else if (gpus->selected_gpus().size() > 1) {
        // When there are multiple selected GPUs, use GPU+index or matching gpu_text
        gpu_text = "GPU" + std::to_string(index);
        if (gpus->params->gpu_text.size() > index) {
            gpu_text = gpus->params->gpu_text[index];
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
