#ifdef HAVE_NVML
#include "nvml.h"
#endif
#include "hud_elements.h"
#include "logging.h"
#include "string_utils.h"
#include <thread>
#include <chrono>
#include "mesa/util/macros.h"

NVIDIA::NVIDIA(const char* pciBusId) {
#ifdef HAVE_NVML
    if (nvml && nvml->IsLoaded()) {
        nvmlReturn_t result = nvml->nvmlInit_v2();
        if (NVML_SUCCESS != result) {
            SPDLOG_ERROR("Nvidia module initialization failed: {}", nvml->nvmlErrorString(result));
            nvml_available = false;
        } else {
            nvml_available = true; // NVML initialized successfully
            if (pciBusId) {
                result = nvml->nvmlDeviceGetHandleByPciBusId_v2(pciBusId, &device);
                if (NVML_SUCCESS != result) {
                    SPDLOG_ERROR("Getting device handle by PCI bus ID failed: {}", nvml->nvmlErrorString(result));
                    nvml_available = false; // Revert if getting device handle fails
                }
            }
        }
    }
#endif

    if (nvml_available || nvctrl_available) {
        throttling = std::make_shared<Throttling>(0x10de);
        thread = std::thread(&NVIDIA::get_samples_and_copy, this);
        pthread_setname_np(thread.native_handle(), "mangohud-nvidia");
    } else {
        SPDLOG_WARN("NVML and NVCTRL are unavailable. Unable to get NVIDIA info. User is on DFSG version of mangohud?");
    }
}

#ifdef HAVE_NVML
void NVIDIA::get_instant_metrics_nvml(struct gpu_metrics *metrics) {
    auto params = HUDElements.params;
    nvmlReturn_t response;

    if (nvml && nvml_available) {
        nvml_get_process_info();

        struct nvmlUtilization_st nvml_utilization;
        response = nvml->nvmlDeviceGetUtilizationRates(device, &nvml_utilization);
        if (response == NVML_ERROR_NOT_SUPPORTED) {
            if (nvml_available)
                SPDLOG_ERROR("nvmlDeviceGetUtilizationRates failed, disabling nvml metrics");
            nvml_available = false;
        }
        metrics->load = nvml_utilization.gpu;

        if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_temp] || (logger && logger->is_active())) {
            unsigned int temp;
            nvml->nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp);
            metrics->temp = temp;
        }

        if (params->enabled[OVERLAY_PARAM_ENABLED_vram] || (logger && logger->is_active())) {
            struct nvmlMemory_st nvml_memory;
            nvml->nvmlDeviceGetMemoryInfo(device, &nvml_memory);
            metrics->memoryTotal = nvml_memory.total / (1024.f * 1024.f * 1024.f);
            metrics->sys_vram_used = nvml_memory.used / (1024.f * 1024.f * 1024.f);
        }

        if (params->enabled[OVERLAY_PARAM_ENABLED_proc_vram])
            metrics->proc_vram_used = get_proc_vram() / (1024.f * 1024.f * 1024.f);

        if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_core_clock] || (logger && logger->is_active())) {
            unsigned int core_clock;
            nvml->nvmlDeviceGetClockInfo(device, NVML_CLOCK_GRAPHICS, &core_clock);
            metrics->CoreClock = core_clock;
        }

        if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_mem_clock] || (logger && logger->is_active())) {
            unsigned int memory_clock;
            nvml->nvmlDeviceGetClockInfo(device, NVML_CLOCK_MEM, &memory_clock);
            metrics->MemClock = memory_clock;
        }

        if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_power] || (logger && logger->is_active())) {
            unsigned int power, limit;
            nvml->nvmlDeviceGetPowerUsage(device, &power);
            nvml->nvmlDeviceGetPowerManagementLimit(device, &limit);
            metrics->powerUsage = power / 1000;
            metrics->powerLimit = limit / 1000;
        }

        if (params->enabled[OVERLAY_PARAM_ENABLED_throttling_status]) {
            unsigned long long nvml_throttle_reasons;
            nvml->nvmlDeviceGetCurrentClocksThrottleReasons(device, &nvml_throttle_reasons);
            metrics->is_temp_throttled = (nvml_throttle_reasons & 0x0000000000000060LL) != 0;
            metrics->is_power_throttled = (nvml_throttle_reasons & 0x000000000000008CLL) != 0;
            metrics->is_other_throttled = (nvml_throttle_reasons & 0x0000000000000112LL) != 0;
            if (throttling)
		        throttling->indep_throttle_status = nvml_throttle_reasons;
        }

        if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_fan] || (logger && logger->is_active())){
            unsigned int fan_speed;
            nvml->nvmlDeviceGetFanSpeed(device, &fan_speed);
            metrics->fan_speed = fan_speed;
            metrics->fan_rpm = false;
        }
    }
}
#endif

void NVIDIA::get_samples_and_copy() {
    struct gpu_metrics metrics_buffer[METRICS_SAMPLE_COUNT] {};
    while(!stop_thread) {
#ifndef TEST_ONLY
        if (HUDElements.g_gamescopePid > 0 && HUDElements.g_gamescopePid != pid) {
            pid = HUDElements.g_gamescopePid;
        }
#endif

        for (size_t cur_sample_id=0; cur_sample_id < METRICS_SAMPLE_COUNT; cur_sample_id++) {
#ifdef HAVE_NVML
        if (nvml_available)
            NVIDIA::get_instant_metrics_nvml(&metrics_buffer[cur_sample_id]);
#endif
            usleep(METRICS_POLLING_PERIOD_MS * 1000);
        }

        if (stop_thread) break;

        std::unique_lock<std::mutex> lock(metrics_mutex);
        cond_var.wait(lock, [this]() { return !paused || stop_thread; });
        GPU_UPDATE_METRIC_AVERAGE(load);
        GPU_UPDATE_METRIC_AVERAGE_FLOAT(powerUsage);
        GPU_UPDATE_METRIC_MAX(powerLimit);
        GPU_UPDATE_METRIC_AVERAGE(CoreClock);
        GPU_UPDATE_METRIC_AVERAGE(MemClock);

        GPU_UPDATE_METRIC_AVERAGE(temp);

        GPU_UPDATE_METRIC_AVERAGE_FLOAT(memoryTotal);
        GPU_UPDATE_METRIC_AVERAGE_FLOAT(sys_vram_used);
        GPU_UPDATE_METRIC_AVERAGE_FLOAT(proc_vram_used);

        GPU_UPDATE_METRIC_MAX(is_power_throttled);
        GPU_UPDATE_METRIC_MAX(is_current_throttled);
        GPU_UPDATE_METRIC_MAX(is_temp_throttled);
        GPU_UPDATE_METRIC_MAX(is_other_throttled);

        GPU_UPDATE_METRIC_MAX(fan_speed);
    }
}
