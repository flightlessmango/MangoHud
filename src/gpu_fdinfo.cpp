#ifndef TEST_ONLY
#include "hud_elements.h"
#endif

#include "gpu_fdinfo.h"

GPU_fdinfo::GPU_fdinfo(
    const std::string driver, const std::string pci_dev, const std::string drm_node,
    uint32_t device_id, uint32_t vendor_id
) : driver(driver), pci_dev(pci_dev), drm_node(drm_node),
    gpu(driver, drm_node, pci_dev, vendor_id, device_id)
{
    SPDLOG_DEBUG("GPU driver is \"{}\"", driver);

    gpu.add_pid(pid);

    thread = std::thread(&GPU_fdinfo::main_thread, this);
    // "mangohud-gpufdinfo" wouldn't fit in the 15 byte limit
    pthread_setname_np(thread.native_handle(), "mangohud-gpufd");
}

void GPU_fdinfo::main_thread()
{
    while (!stop_thread) {
        std::unique_lock<std::mutex> lock(metrics_mutex);
        cond_var.wait(lock, [this]() { return !paused || stop_thread; });

#ifndef TEST_ONLY
        if (HUDElements.g_gamescopePid > 0 && HUDElements.g_gamescopePid != pid) {
            pid = HUDElements.g_gamescopePid;
            gpu.add_pid(pid);
        }
#endif

        gpu.poll();

        if (driver == "msm_drm") {
            metrics.load = gpu.get_load();
        } else {
            metrics.load = gpu.get_process_load(pid);
        }

        metrics.temp = gpu.get_temperature();
        metrics.junction_temp = gpu.get_junction_temperature();
        metrics.memory_temp = gpu.get_memory_temp();
        metrics.sys_vram_used = gpu.get_vram_used();
        metrics.proc_vram_used = gpu.get_process_vram_used(pid);
        metrics.memoryTotal = gpu.get_memory_total();
        metrics.MemClock = gpu.get_memory_clock();
        metrics.CoreClock = gpu.get_core_clock();
        metrics.powerUsage = gpu.get_power_usage();
        metrics.powerLimit = gpu.get_power_limit();
        metrics.is_power_throttled = gpu.get_is_power_throttled();
        metrics.is_current_throttled = gpu.get_is_current_throttled();
        metrics.is_temp_throttled = gpu.get_is_temp_throttled();
        metrics.is_other_throttled = gpu.get_is_other_throttled();
        metrics.gtt_used = gpu.get_gtt_used();
        metrics.fan_speed = gpu.get_fan_speed();
        metrics.voltage = gpu.get_voltage();
        metrics.fan_rpm = gpu.get_fan_rpm();

        SPDLOG_DEBUG(
            "pci_dev = {}, pid = {}, driver = {}, "
            "load = {}, temp = {}, junction_temp = {}, memory_temp = {}, sys_vram_used = {}, "
            "proc_vram_used = {}, memoryTotal = {}, MemClock = {}, CoreClock = {}, powerUsage = {}, "
            "powerLimit = {}, is_power_throttled = {}, is_current_throttled = {}, "
            "is_temp_throttled = {}, is_other_throttled = {}, gtt_used = {}, fan_speed = {}, "
            "voltage = {}, fan_rpm = {}",
            pci_dev, pid, driver,
            metrics.load, metrics.temp, metrics.junction_temp, metrics.memory_temp,
            metrics.sys_vram_used, metrics.proc_vram_used, metrics.memoryTotal, metrics.MemClock,
            metrics.CoreClock, metrics.powerUsage, metrics.powerLimit, metrics.is_power_throttled,
            metrics.is_current_throttled, metrics.is_temp_throttled, metrics.is_other_throttled,
            metrics.gtt_used, metrics.fan_speed, metrics.voltage, metrics.fan_rpm
        );

        std::this_thread::sleep_for(
            std::chrono::milliseconds(METRICS_UPDATE_PERIOD_MS)
        );
    }
}
