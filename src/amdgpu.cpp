#include <spdlog/spdlog.h>
#include <thread>
#include "amdgpu.h"
#include "gpu.h"
#include "cpu.h"
#include "overlay.h"
#include "hud_elements.h"
#include "logging.h"
#include "mesa/util/macros.h"

void AMDGPU::get_samples_and_copy(struct amdgpu_common_metrics metrics_buffer[METRICS_SAMPLE_COUNT], bool &gpu_load_needs_dividing) {
	while (!stop_thread) {
		size_t sample_count = 0;
		// Get all the samples
		for (size_t cur_sample_id=0; cur_sample_id < METRICS_SAMPLE_COUNT; cur_sample_id++) {
			struct amdgpu_common_metrics sample = {};
			if (gpu_metrics_is_valid && get_instant_metrics(&sample)) {
				// Detect and fix if the gpu load is reported in centipercent
				if (gpu_load_needs_dividing || sample.gpu_load_percent > 100) {
					gpu_load_needs_dividing = true;
					sample.gpu_load_percent /= 100;
				}
				metrics_buffer[sample_count++] = sample;
			}

			// frequently check if thread should stop so we don't get stuck for 500ms
			for (int ms = 0; ms < METRICS_POLLING_PERIOD_MS; ms++) {
				if (stop_thread) break;
				usleep(1000);
			}
		}

		if (stop_thread) break;

		std::unique_lock<std::mutex> lock(metrics_mutex);
		cond_var.wait(lock, [this]() { return !paused || stop_thread; });
		lock.unlock();

#ifndef TEST_ONLY
		if (HUDElements.g_gamescopePid > 0 && HUDElements.g_gamescopePid != pid) {
			pid = HUDElements.g_gamescopePid;
			fdinfo.add_pid(pid);
		}
#endif

		fdinfo.poll_all();
		float proc_vram_used = fdinfo.get_memory_used(pid, "drm-memory-vram");

		lock.lock();
		// do one pass of metrics from sysfs nodes
		// then we replace with GPU metrics if it's available
		get_sysfs_metrics();
		metrics.proc_vram_used = proc_vram_used;

		if (sample_count > 0) {
			UPDATE_METRIC_AVERAGE(gpu_load_percent);
			UPDATE_METRIC_AVERAGE_FLOAT(average_gfx_power_w);
			UPDATE_METRIC_AVERAGE_FLOAT(average_cpu_power_w);

			UPDATE_METRIC_AVERAGE(current_gfxclk_mhz);
			UPDATE_METRIC_AVERAGE(current_uclk_mhz);

			UPDATE_METRIC_AVERAGE(soc_temp_c);
			UPDATE_METRIC_AVERAGE(gpu_temp_c);
			UPDATE_METRIC_AVERAGE(apu_cpu_temp_c);

			UPDATE_METRIC_MAX(is_power_throttled);
			UPDATE_METRIC_MAX(is_current_throttled);
			UPDATE_METRIC_MAX(is_temp_throttled);
			UPDATE_METRIC_MAX(is_other_throttled);

			UPDATE_METRIC_MAX(fan_speed);
			metrics.fan_rpm = true;

			metrics.load = amdgpu_common_metrics.gpu_load_percent;
			metrics.powerUsage = amdgpu_common_metrics.average_gfx_power_w;
			metrics.MemClock = amdgpu_common_metrics.current_uclk_mhz;

			// Use hwmon instead, see gpu.cpp
			if ( device_id == 0x1435 || device_id == 0x163f )
			{
				// If we are on VANGOGH (Steam Deck), then
				// always use core clock from GPU metrics.
				metrics.CoreClock = amdgpu_common_metrics.current_gfxclk_mhz;
			}
			metrics.temp = amdgpu_common_metrics.gpu_temp_c;
			metrics.apu_cpu_power = amdgpu_common_metrics.average_cpu_power_w;
			metrics.apu_cpu_temp = amdgpu_common_metrics.apu_cpu_temp_c;

			metrics.is_power_throttled = amdgpu_common_metrics.is_power_throttled;
			metrics.is_current_throttled = amdgpu_common_metrics.is_current_throttled;
			metrics.is_temp_throttled = amdgpu_common_metrics.is_temp_throttled;
			metrics.is_other_throttled = amdgpu_common_metrics.is_other_throttled;

			// Set only if gpu_metrics has value larger than 0, otherwise use hwmon
			if (amdgpu_common_metrics.fan_speed > 0)
				metrics.fan_speed = amdgpu_common_metrics.fan_speed;
		}
	}
}

void AMDGPU::metrics_polling_thread() {
	struct amdgpu_common_metrics metrics_buffer[METRICS_SAMPLE_COUNT];
	bool gpu_load_needs_dividing = false;  //some GPUs report load as centipercent

	// Initial poll of the metrics, so that we have values to display as fast as possible
	if (get_instant_metrics(&amdgpu_common_metrics) && amdgpu_common_metrics.gpu_load_percent > 100){
		gpu_load_needs_dividing = true;
		amdgpu_common_metrics.gpu_load_percent /= 100;
	}

	// Set all the fields to 0 by default. Only done once as we're just replacing previous values after
	memset(metrics_buffer, 0, sizeof(metrics_buffer));

	while (!stop_thread) {
#ifndef TEST_ONLY
		if (get_params()->no_display && !logger->is_active())
			usleep(100000);
		else
#endif
			get_samples_and_copy(metrics_buffer, gpu_load_needs_dividing);
	}
}

AMDGPU::AMDGPU(std::string pci_dev, uint32_t device_id, uint32_t vendor_id, std::string drm_node)
: fdinfo(drm_node) {
	this->pci_dev = pci_dev;
	this->device_id = device_id;
	this->vendor_id = vendor_id;
	platform_init(drm_node);

	fdinfo.add_pid(pid);

	throttling = std::make_shared<Throttling>(0x1002);
	thread = std::thread(&AMDGPU::metrics_polling_thread, this);
	pthread_setname_np(thread.native_handle(), "mangohud-amdgpu");
}
