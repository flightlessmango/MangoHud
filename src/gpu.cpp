#include "gpu.h"
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
        if (entry.is_directory()) {
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
    }

    // Sort the entries based on the numeric value of the render number
    std::sort(gpu_entries.begin(), gpu_entries.end(), [](const std::string& a, const std::string& b) {
        int num_a = std::stoi(a.substr(7));
        int num_b = std::stoi(b.substr(7));
        return num_a < num_b;
    });

    // Now process the sorted GPU entries
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
        available_gpus.emplace_back(ptr);

        SPDLOG_DEBUG("GPU Found: node_name: {}, vendor_id: {:x} device_id: {:x} pci_dev: {}", node_name, vendor_id, device_id, pci_dev);
    }

    find_active_gpu();
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

void GPUS::find_active_gpu() {
    pid_t pid = getpid();
    std::string fdinfo_dir = "/proc/" + std::to_string(pid) + "/fdinfo/";
    bool active_gpu_found = false;

    for (const auto& entry : fs::directory_iterator(fdinfo_dir)) {
        if (entry.is_regular_file()) {
            std::ifstream file(entry.path().string());
            std::string line;
            std::string drm_pdev;
            bool has_drm_driver = false;
            bool has_drm_engine_gfx = false;

            while (std::getline(file, line)) {
                if (line.find("drm-driver:") != std::string::npos) {
                    has_drm_driver = true;
                }
                if (line.find("drm-pdev:") != std::string::npos) {
                    drm_pdev = line.substr(line.find(":") + 1);
                    drm_pdev.erase(0, drm_pdev.find_first_not_of(" \t"));
                }
                if (line.find("drm-engine-gfx:") != std::string::npos) {
                    uint64_t gfx_time = std::stoull(line.substr(line.find(":") + 1));
                    if (gfx_time > 0) {
                        has_drm_engine_gfx = true;
                    }
                }
            }

            if (has_drm_driver && has_drm_engine_gfx) {
                for (const auto& gpu : available_gpus) {
                    if (gpu->pci_dev == drm_pdev) {
                        gpu->is_active = true;
                        SPDLOG_DEBUG("Active GPU Found: node_name: {}, pci_dev: {}", gpu->name, gpu->pci_dev);
                    } else {
                        gpu->is_active = false;
                    }
                }
                return;
            }
        }
    }

    // NVIDIA GPUs will not show up in fdinfo so we use NVML instead to find the active GPU
    // This will not work for older NVIDIA GPUs
#ifdef HAVE_NVML
    if (!active_gpu_found) {
        for (const auto& gpu : available_gpus) {
            // NVIDIA vendor ID is 0x10de
            if (gpu->vendor_id == 0x10de && gpu->nvidia->nvml_available) { 
                for (auto& pid : gpu->nvidia_pids()) {
                    if (pid == getpid()) {
                        gpu->is_active = true;
                        SPDLOG_DEBUG("Active GPU Found: node_name: {}, pci_dev: {}", gpu->name, gpu->pci_dev);
                        return;
                    }
                }

            }
        }
    }
#endif
    SPDLOG_DEBUG("failed to find active GPU");
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
