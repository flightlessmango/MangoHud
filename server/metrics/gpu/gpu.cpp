#include <algorithm>

#include "gpu.hpp"
#include "intel/i915/i915.hpp"
#include "intel/xe/xe.hpp"
#include "amdgpu/amdgpu.hpp"
#include "nvidia/nvidia.hpp"
#include "panfrost.hpp"
#include "msm/dpu.hpp"
#include "msm/kgsl.hpp"
#include "../common/helpers.hpp"

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
        } else if (driver == "msm_dpu") {
            gpu = std::make_shared<MSM_DPU>(drm_node, pci_dev, vendor_id, device_id);
        } else if (driver == "msm_drm") {
            gpu = std::make_shared<MSM_KGSL>(drm_node, pci_dev, vendor_id, device_id);
        } else {
            continue;
        }

        available_gpus.push_back(gpu);
        gpu->start_thread_worker();

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

void GPU::check_pids_existence() {
    std::set<pid_t> pids_to_delete;

    for (const auto& p : process_metrics) {
        pid_t pid = p.first;

        if (!fs::exists("/proc/" + std::to_string(pid)))
            pids_to_delete.insert(pid);
    }

    for (const auto& p : pids_to_delete)
        process_metrics.erase(p);
}

void GPU::poll() {
    while (!stop_thread) {
        SPDLOG_TRACE("poll()");

        auto current_time = std::chrono::steady_clock::now();
        delta_time_ns = current_time - previous_time;
        previous_time = current_time;

        pre_poll_overrides();

        gpu_metrics_system_t cur_sys_metrics = {
            .load                   = get_load(),

            .vram_used              = get_vram_used(),
            .gtt_used               = get_gtt_used(),
            .memory_total           = get_memory_total(),
            .memory_clock           = get_memory_clock(),
            .memory_temp            = get_memory_temp(),

            .temperature            = get_temperature(),
            .junction_temperature   = get_junction_temperature(),

            .core_clock             = get_core_clock(),
            .voltage                = get_voltage(),

            .power_usage            = get_power_usage(),
            .power_limit            = get_power_limit(),

            .is_apu                 = get_is_apu(),
            .apu_cpu_power          = get_apu_cpu_power(),
            .apu_cpu_temp           = get_apu_cpu_temp(),

            .is_power_throttled     = get_is_power_throttled(),
            .is_current_throttled   = get_is_current_throttled(),
            .is_temp_throttled      = get_is_temp_throttled(),
            .is_other_throttled     = get_is_other_throttled(),

            .fan_speed              = get_fan_speed(),
            .fan_rpm                = get_fan_rpm()
        };

        check_pids_existence();

        std::map<pid_t, gpu_metrics_process_t> cur_proc_metrics = process_metrics;

        for (auto& p : cur_proc_metrics) {
            pid_t pid = p.first;
            gpu_metrics_process_t* m = &p.second;

            m->load = get_process_load(pid);
            m->vram_used = get_process_vram_used(pid);
            m->gtt_used = get_process_gtt_used(pid);
        }

        {
            std::unique_lock sys_lock(system_metrics_mutex);
            std::unique_lock proc_lock(process_metrics_mutex);
            system_metrics = cur_sys_metrics;
            process_metrics = cur_proc_metrics;
        }

        std::this_thread::sleep_for(1s);
    }
}

GPU::GPU(
    const std::string& drm_node, const std::string& pci_dev,
    uint16_t vendor_id, uint16_t device_id, const std::string& thread_name
) : drm_node(drm_node), pci_dev(pci_dev), vendor_id(vendor_id),
    device_id(device_id), worker_thread_name(thread_name) {}

GPU::~GPU() {
    stop_thread = true;
    if (worker_thread.joinable())
        worker_thread.join();
}

void GPU::add_pid(pid_t pid) {
    std::unique_lock lock(process_metrics_mutex);
    process_metrics.try_emplace(pid, gpu_metrics_process_t());
}

gpu_metrics_system_t GPU::get_system_metrics() {
    SPDLOG_TRACE("GPU get_system_metrics()");
    std::unique_lock lock(system_metrics_mutex);
    return system_metrics;
}

std::map<pid_t, gpu_metrics_process_t> GPU::get_process_metrics() {
    SPDLOG_TRACE("GPU get_process_metrics");
    std::unique_lock lock(process_metrics_mutex);
    return process_metrics;
}

gpu_metrics_process_t GPU::get_process_metrics(const size_t pid) {
    SPDLOG_TRACE("GPU get_process_metrics");
    std::unique_lock lock(process_metrics_mutex);
    return process_metrics[pid];
}

void GPU::print_metrics() {
    std::unique_lock sys_lock(system_metrics_mutex);
    std::unique_lock proc_lock(process_metrics_mutex);

    SPDLOG_TRACE("==========================");
    SPDLOG_TRACE("load                 = {}\n", system_metrics.load);

    // SPDLOG_TRACE("vram_used            = {}", vram_used);
    // SPDLOG_TRACE("gtt_used             = {}", gtt_used);
    // SPDLOG_TRACE("memory_total         = {}", memory_total);
    // SPDLOG_TRACE("memory_clock         = {}", memory_clock);
    // SPDLOG_TRACE("memory_temp          = {}\n", memory_temp);

    // SPDLOG_TRACE("temperature          = {}", temperature);
    // SPDLOG_TRACE("junction_temperature = {}\n", junction_temperature);

    // SPDLOG_TRACE("core_clock           = {}", core_clock);
    SPDLOG_TRACE("voltage              = {}\n", system_metrics.voltage);

    SPDLOG_TRACE("power_usage          = {}", system_metrics.power_usage);
    SPDLOG_TRACE("power_limit          = {}\n", system_metrics.power_limit);

    // SPDLOG_TRACE("apu_cpu_power        = {}", apu_cpu_power);
    // SPDLOG_TRACE("apu_cpu_temp         = {}\n", apu_cpu_temp);

    // SPDLOG_TRACE("is_power_throttled   = {}", is_power_throttled);
    // SPDLOG_TRACE("is_current_throttled = {}", is_current_throttled);
    // SPDLOG_TRACE("is_temp_throttled    = {}", is_temp_throttled);
    // SPDLOG_TRACE("is_other_throttled   = {}\n", is_other_throttled);

    // SPDLOG_TRACE("fan_speed            = {}", fan_speed);
    // SPDLOG_TRACE("fan_rpm              = {}\n", fan_rpm);

    SPDLOG_TRACE("Process stats:");

    for (const auto& p : process_metrics) {
        pid_t pid = p.first;
        gpu_metrics_process_t m = p.second;
        SPDLOG_TRACE("    {}:", pid);
        SPDLOG_TRACE("        load      = {}", m.load);
        SPDLOG_TRACE("        vram_used = {}", m.vram_used);
        SPDLOG_TRACE("        gtt_used  = {}\n", m.gtt_used);
    }

    SPDLOG_TRACE("==========================\n");
}

void GPU::start_thread_worker() {
    worker_thread = std::thread(&GPU::poll, this);

    if (worker_thread_name.length() > 15)
        SPDLOG_DEBUG(
            "thread name \"{}\" is longer than allowed linux maximum of 15 characters!"
        );

    pthread_setname_np(worker_thread.native_handle(), worker_thread_name.c_str());
}
