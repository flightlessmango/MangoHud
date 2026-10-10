#ifdef HAVE_NVML
#include "nvml.h"
#endif
#include "hud_elements.h"
#include "logging.h"
#include "string_utils.h"
#include <thread>
#include <chrono>
#include "mesa/util/macros.h"

#if defined(HAVE_XNVCTRL) && defined(HAVE_X11)
void NVIDIA::parse_token(const std::string& token, std::unordered_map<std::string, std::string>& options) {
    std::string param, value;

    size_t equal = token.find('=');
    if (equal == std::string::npos)
        return;

    value = token.substr(equal+1);

    param = token.substr(0, equal);
    trim(param);
    trim(value);
    if (!param.empty())
        options[param] = value;
}
#endif

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

#if defined(HAVE_XNVCTRL) && defined(HAVE_X11)
    if (!get_libx11()->IsLoaded())
        SPDLOG_DEBUG("XNVCtrl: X11 not loaded");

    if (!nvctrl || !nvctrl->IsLoaded()) {
        SPDLOG_DEBUG("XNVCtrl loader failed to load");
        nvctrl_available = false;
    } else {
        nvctrl_available = find_nv_x11(display);
    }

    if (nvctrl && nvctrl_available) {
        nvctrl->XNVCTRLQueryTargetCount(display,
            NV_CTRL_TARGET_TYPE_COOLER,
            &num_coolers);
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
void NVIDIA::get_instant_metrics_nvml(struct gpu_metrics *metrics, struct overlay_params *params) {
    nvmlReturn_t response;

    if (nvml && nvml_available) {
        static const unsigned int slow_poll_divisor =
            MAX(1u, (unsigned int)(NVML_SLOW_POLL_PERIOD_MS / METRICS_POLLING_PERIOD_MS));
        const bool slow_tick = (slow_sample_counter == 0);
        const bool log_active = (logger && logger->is_active());

        slow_sample_counter = (slow_sample_counter + 1) % slow_poll_divisor;

        struct nvmlUtilization_st nvml_utilization;
        response = nvml->nvmlDeviceGetUtilizationRates(device, &nvml_utilization);
        if (response == NVML_ERROR_NOT_SUPPORTED) {
            SPDLOG_ERROR("nvmlDeviceGetUtilizationRates failed, disabling nvml metrics");

            nvml_available = false;
            return;
        }

        metrics->load = nvml_utilization.gpu;

        if (slow_tick) {
            if (params->enabled[OVERLAY_PARAM_ENABLED_proc_vram] || log_active) {
                nvml_get_process_info();
                cached_slow_metrics.proc_vram_used = get_proc_vram() / (1024.f * 1024.f * 1024.f);
            }

            if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_temp] || log_active) {
                unsigned int temp;
                nvml->nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temp);
                cached_slow_metrics.temp = temp;
            }

            if (params->enabled[OVERLAY_PARAM_ENABLED_vram] || log_active) {
                const float to_gib = 1024.f * 1024.f * 1024.f;

                if (nvml->nvmlDeviceGetMemoryInfo_v2) {
                    nvmlMemory_v2_t nvml_memory_v2 {};
                    nvml_memory_v2.version = nvmlMemory_v2;

                    if (nvml->nvmlDeviceGetMemoryInfo_v2(device, &nvml_memory_v2) == NVML_SUCCESS) {
                        cached_slow_metrics.memoryTotal = nvml_memory_v2.total / to_gib;
                        cached_slow_metrics.sys_vram_used = nvml_memory_v2.used / to_gib;
                    }
                } else {
                    struct nvmlMemory_st nvml_memory;

                    if (nvml->nvmlDeviceGetMemoryInfo(device, &nvml_memory) == NVML_SUCCESS) {
                        cached_slow_metrics.memoryTotal = nvml_memory.total / to_gib;
                        cached_slow_metrics.sys_vram_used = nvml_memory.used / to_gib;
                    }
                }
            }

            if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_core_clock] || log_active) {
                unsigned int core_clock;
                nvml->nvmlDeviceGetClockInfo(device, NVML_CLOCK_GRAPHICS, &core_clock);
                cached_slow_metrics.CoreClock = core_clock;
            }

            if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_mem_clock] || log_active) {
                unsigned int memory_clock;
                nvml->nvmlDeviceGetClockInfo(device, NVML_CLOCK_MEM, &memory_clock);
                cached_slow_metrics.MemClock = memory_clock;
            }

            if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_power] || log_active) {
                unsigned int power, limit;
                nvml->nvmlDeviceGetPowerUsage(device, &power);
                nvml->nvmlDeviceGetPowerManagementLimit(device, &limit);
                cached_slow_metrics.powerUsage = power / 1000;
                cached_slow_metrics.powerLimit = limit / 1000;
            }

            if (params->enabled[OVERLAY_PARAM_ENABLED_throttling_status]) {
                unsigned long long nvml_throttle_reasons;
                nvml->nvmlDeviceGetCurrentClocksThrottleReasons(device, &nvml_throttle_reasons);
                cached_slow_metrics.is_temp_throttled = (nvml_throttle_reasons & 0x0000000000000060LL) != 0;
                cached_slow_metrics.is_power_throttled = (nvml_throttle_reasons & 0x000000000000008CLL) != 0;
                cached_slow_metrics.is_other_throttled = (nvml_throttle_reasons & 0x0000000000000112LL) != 0;
                if (throttling)
                    throttling->indep_throttle_status = nvml_throttle_reasons;
            }

            if (params->enabled[OVERLAY_PARAM_ENABLED_gpu_fan] || log_active) {
                unsigned int fan_speed;
                nvmlReturn_t fan_response;

                if (nvml->nvmlDeviceGetFanSpeed_v2)
                    fan_response = nvml->nvmlDeviceGetFanSpeed_v2(device, 0, &fan_speed);
                else
                    fan_response = nvml->nvmlDeviceGetFanSpeed(device, &fan_speed);

                if (fan_response == NVML_SUCCESS)
                    cached_slow_metrics.fan_speed = fan_speed;

                cached_slow_metrics.fan_rpm = false;
            }
        }

        metrics->proc_vram_used = cached_slow_metrics.proc_vram_used;
        metrics->temp = cached_slow_metrics.temp;
        metrics->memoryTotal = cached_slow_metrics.memoryTotal;
        metrics->sys_vram_used = cached_slow_metrics.sys_vram_used;
        metrics->CoreClock = cached_slow_metrics.CoreClock;
        metrics->MemClock = cached_slow_metrics.MemClock;
        metrics->powerUsage = cached_slow_metrics.powerUsage;
        metrics->powerLimit = cached_slow_metrics.powerLimit;
        metrics->is_temp_throttled = cached_slow_metrics.is_temp_throttled;
        metrics->is_power_throttled = cached_slow_metrics.is_power_throttled;
        metrics->is_other_throttled = cached_slow_metrics.is_other_throttled;
        metrics->fan_speed = cached_slow_metrics.fan_speed;
        metrics->fan_rpm = cached_slow_metrics.fan_rpm;
    #ifdef HAVE_XNVCTRL
        if (nvctrl_available) {
            metrics->fan_rpm = true;
            metrics->fan_speed = NVIDIA::get_nvctrl_fan_speed();
        }
    #endif
    }
}
#endif

#if defined(HAVE_XNVCTRL) && defined(HAVE_X11)
void NVIDIA::get_instant_metrics_xnvctrl(struct gpu_metrics *metrics) {
    std::unordered_map<std::string, std::string> xnvctrl_params;
    std::string token;

    if (!display)
        nvctrl_available = false;

    if (nvctrl && nvctrl_available && !nvml_available) {

        int enums[] = {
            NV_CTRL_STRING_GPU_UTILIZATION,
            NV_CTRL_STRING_GPU_CURRENT_CLOCK_FREQS,
            0 // keep null
        };

        for (size_t i=0; enums[i]; i++) {
            char* str = get_attr_target_string(enums[i], NV_CTRL_TARGET_TYPE_GPU, 0);
            if (!str)
                continue;

            std::stringstream ss (str);
            while (std::getline(ss, token, ',')) {
                parse_token(token, xnvctrl_params);
            }
            free(str);
        }

        if (!try_stoi(metrics->load, xnvctrl_params["graphics"]))
            metrics->load = 0;
        if (!try_stoi(metrics->CoreClock, xnvctrl_params["nvclock"]))
            metrics->CoreClock = 0;
        if (!try_stoi(metrics->MemClock, xnvctrl_params["memclock"]))
            metrics->MemClock = 0;

        int64_t temp = 0;
        nvctrl->XNVCTRLQueryTargetAttribute64(display,
                            NV_CTRL_TARGET_TYPE_GPU,
                            0,
                            0,
                            NV_CTRL_GPU_CORE_TEMPERATURE,
                            &temp);
        metrics->temp = temp;

        int64_t memtotal = 0;
        nvctrl->XNVCTRLQueryTargetAttribute64(display,
                            NV_CTRL_TARGET_TYPE_GPU,
                            0,
                            0,
                            NV_CTRL_TOTAL_DEDICATED_GPU_MEMORY,
                            &memtotal);
        metrics->memoryTotal = static_cast<float>(memtotal) / 1024.f;

        int64_t memused = 0;
        nvctrl->XNVCTRLQueryTargetAttribute64(display,
                            NV_CTRL_TARGET_TYPE_GPU,
                            0,
                            0,
                            NV_CTRL_USED_DEDICATED_GPU_MEMORY,
                            &memused);
        metrics->sys_vram_used = static_cast<float>(memused) / 1024.f;

        metrics->fan_speed = NVIDIA::get_nvctrl_fan_speed();
    }
}
#endif

void NVIDIA::get_samples_and_copy() {
    struct gpu_metrics metrics_buffer[METRICS_SAMPLE_COUNT] {};
    auto logger_ref = logger; // inc ref count, to avoid destruction of logger.
    while(!stop_thread) {
        auto params = get_params();
        auto params_p = params.get(); // avoid destruction while we are getting samples...

#ifndef TEST_ONLY
        if (HUDElements.g_gamescopePid > 0 && HUDElements.g_gamescopePid != pid) {
            pid = HUDElements.g_gamescopePid;
        }
#endif

        for (size_t cur_sample_id=0; cur_sample_id < METRICS_SAMPLE_COUNT; cur_sample_id++) {
#ifdef HAVE_NVML
        if (nvml_available)
            NVIDIA::get_instant_metrics_nvml(&metrics_buffer[cur_sample_id], params_p);
#endif
#if defined(HAVE_XNVCTRL) && defined(HAVE_X11)
        if (nvctrl_available)
            NVIDIA::get_instant_metrics_xnvctrl(&metrics_buffer[cur_sample_id]);
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

#if defined(HAVE_XNVCTRL) && defined(HAVE_X11)
int64_t NVIDIA::get_nvctrl_fan_speed(){
    int64_t fan_speed = 0;
    if (num_coolers >= 1) {
        nvctrl->XNVCTRLQueryTargetAttribute64(display,
                            NV_CTRL_TARGET_TYPE_COOLER,
                            0,
                            0,
                            NV_CTRL_THERMAL_COOLER_SPEED,
                            &fan_speed);
    }
    metrics.fan_rpm = true;
    return fan_speed;
}
#endif

#ifdef HAVE_XNVCTRL
char* NVIDIA::get_attr_target_string(int attr, int target_type, int target_id) {
    char* c = nullptr;
    if (nvctrl && !nvctrl->XNVCTRLQueryTargetStringAttribute(NVIDIA::display, target_type, target_id, 0, attr, &c)) {
        SPDLOG_ERROR("Failed to query attribute '{}'", attr);
    }
    return c;
}
#endif

#if defined(HAVE_XNVCTRL) && defined(HAVE_X11)
bool NVIDIA::find_nv_x11(Display*& dpy)
{
    const char *displayid = getenv("DISPLAY");
    auto libx11 = get_libx11();
    if (displayid) {
        Display *d = libx11->XOpenDisplay(displayid);
        if (d) {
            int s = libx11->XDefaultScreen(d);
            if (nvctrl && nvctrl->XNVCTRLIsNvScreen(d, s)) {
                dpy = d;
                SPDLOG_DEBUG("XNVCtrl is using display {}", displayid);
                return true;
            }
            libx11->XCloseDisplay(d);
        }
    }
    SPDLOG_DEBUG("XNVCtrl didn't find the correct display");
    return false;
}
#endif
