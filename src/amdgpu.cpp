#include <spdlog/spdlog.h>
#include <thread>
#ifdef __linux__
#include <sys/sysinfo.h>
#endif
#include "amdgpu.h"
#include "gpu.h"
#include "cpu.h"
#include "overlay.h"
#include "hud_elements.h"
#include "logging.h"
#include "mesa/util/macros.h"


#define IS_VALID_METRIC(FIELD) (FIELD != 0xffff)
void AMDGPU::get_instant_metrics(struct amdgpu_common_metrics *metrics) {
	FILE *f;
	uint8_t buf[sizeof(struct gpu_metrics_v3_0)+1];  // big enough for v1.3/v2.4/v3.0
	struct metrics_table_header *header = (struct metrics_table_header *)buf;

	f = fopen(gpu_metrics_path.c_str(), "rb");
	if (!f)
		return;

	size_t nread = fread(buf, 1, sizeof(buf), f);
	fclose(f);

	if (nread < sizeof(*header)) {
		SPDLOG_DEBUG("amdgpu metrics file '{}' may be corrupted (read {} bytes, need at least {})",
					gpu_metrics_path, nread, sizeof(*header));
		return;
	}

	if (nread == sizeof(buf)) {
		// File may be larger than our buffer, so we might have truncated it
		SPDLOG_DEBUG("amdgpu metrics file '{}' may be larger than the buffer ({} bytes)",
					gpu_metrics_path, sizeof(buf));
		return;
	}

	bool is_power=false, is_current=false, is_temp=false, is_other=false;
	if (header->format_revision == 1) {
		// Desktop GPUs
		struct gpu_metrics_v1_3 *amdgpu_metrics = (struct gpu_metrics_v1_3 *) buf;
		metrics->gpu_load_percent = amdgpu_metrics->average_gfx_activity;

		metrics->average_gfx_power_w = amdgpu_metrics->average_socket_power;

		metrics->current_gfxclk_mhz = amdgpu_metrics->current_gfxclk;
		metrics->current_uclk_mhz = amdgpu_metrics->current_uclk;

		metrics->gpu_temp_c = amdgpu_metrics->temperature_edge;
		metrics->fan_speed = amdgpu_metrics->current_fan_speed;

		uint64_t indep = amdgpu_metrics->indep_throttle_status;
		// RDNA 3 almost always shows the TEMP_HOTSPOT throtting flag,
		// so clear that bit
		indep &= ~(1ull << TEMP_HOTSPOT_BIT);  // your existing quirk

		is_power   = ((indep >> 0)  & 0xFF) != 0;
		is_current = ((indep >> 16) & 0xFF) != 0;
		is_temp    = ((indep >> 32) & 0xFFFF) != 0;
		is_other   = ((indep >> 56) & 0xFF) != 0;
		if (throttling)
			throttling->indep_throttle_status = indep;
	} else if (header->format_revision == 2) {
		// APUs
		this->is_apu = true;
		struct gpu_metrics_v2_3 *amdgpu_metrics = (struct gpu_metrics_v2_3 *) buf;

		metrics->gpu_load_percent = amdgpu_metrics->average_gfx_activity;

		metrics->average_gfx_power_w = amdgpu_metrics->average_gfx_power / 1000.f;

		if( IS_VALID_METRIC(amdgpu_metrics->average_cpu_power) ) {
			// prefered method
			metrics->average_cpu_power_w = amdgpu_metrics->average_cpu_power / 1000.f;
		} else if( IS_VALID_METRIC(amdgpu_metrics->average_core_power[0]) ) {
			// fallback 1: sum of core power
			metrics->average_cpu_power_w = 0;
			unsigned i = 0;
			do metrics->average_cpu_power_w = metrics->average_cpu_power_w + amdgpu_metrics->average_core_power[i] / 1000.f;
			while (++i < ARRAY_SIZE(amdgpu_metrics->average_core_power) && IS_VALID_METRIC(amdgpu_metrics->average_core_power[i]));
		} else if( IS_VALID_METRIC(amdgpu_metrics->average_socket_power) && IS_VALID_METRIC(amdgpu_metrics->average_gfx_power) ) {
			// fallback 2: estimate cpu power frostd::string pci_dev, uint32_t deviceID, uint32_t vendorID
			metrics->soc_temp_c = 0;
		}
		if( IS_VALID_METRIC(amdgpu_metrics->temperature_gfx) ) {
			// prefered method
			metrics->gpu_temp_c = amdgpu_metrics->temperature_gfx / 100;
		} else if( header->content_revision >= 3 && IS_VALID_METRIC(amdgpu_metrics->average_temperature_gfx) ) {
			// fallback 1
			metrics->gpu_temp_c = amdgpu_metrics->average_temperature_gfx / 100;
		} else {
			// giving up
			metrics->gpu_temp_c = 0;
		}

		int cpu_temp = 0;
		if( IS_VALID_METRIC(amdgpu_metrics->temperature_core[0]) ) {
			// prefered method
			unsigned i = 0;
			do cpu_temp = MAX(cpu_temp, amdgpu_metrics->temperature_core[i]);
			while (++i < ARRAY_SIZE(amdgpu_metrics->temperature_core) && IS_VALID_METRIC(amdgpu_metrics->temperature_core[i]));
			metrics->apu_cpu_temp_c = cpu_temp / 100;
		} else if( header->content_revision >= 3 && IS_VALID_METRIC(amdgpu_metrics->average_temperature_core[0]) ) {
			// fallback 1
			unsigned i = 0;
			do cpu_temp = MAX(cpu_temp, amdgpu_metrics->average_temperature_core[i]);
			while (++i < ARRAY_SIZE(amdgpu_metrics->average_temperature_core) && IS_VALID_METRIC(amdgpu_metrics->average_temperature_core[i]));
			metrics->apu_cpu_temp_c = cpu_temp / 100;
		} else if( cpuStats.ReadcpuTempFile(cpu_temp) ) {
			// fallback 2: Try temp from file 'm_cpuTempFile' of 'cpu.cpp'
			metrics->apu_cpu_temp_c = cpu_temp;
		} else {
			// giving up
			metrics->apu_cpu_temp_c = 0;
		}

		if( IS_VALID_METRIC(amdgpu_metrics->current_gfxclk) ) {
			// prefered method
			metrics->current_gfxclk_mhz = amdgpu_metrics->current_gfxclk;
		} else if( IS_VALID_METRIC(amdgpu_metrics->average_gfxclk_frequency) ) {
			// fallback 1
			metrics->current_gfxclk_mhz = amdgpu_metrics->average_gfxclk_frequency;
		} else {
			// giving up
			metrics->current_gfxclk_mhz = 0;
		}

		if( IS_VALID_METRIC(amdgpu_metrics->current_uclk) ) {
			// prefered method
			metrics->current_uclk_mhz = amdgpu_metrics->current_uclk;
		} else if( IS_VALID_METRIC(amdgpu_metrics->average_uclk_frequency) ) {
			// fallback 1
			metrics->current_uclk_mhz = amdgpu_metrics->average_uclk_frequency;
		} else {
			// giving up
			metrics->current_uclk_mhz = 0;
		}

		if(header->content_revision >= 2) {
			uint64_t indep = amdgpu_metrics->indep_throttle_status;
			is_power   = ((indep >> 0)  & 0xFF) != 0;
			is_current = ((indep >> 16) & 0xFF) != 0;
			is_temp    = ((indep >> 32) & 0xFFFF) != 0;
			is_other   = ((indep >> 56) & 0xFF) != 0;
		if (throttling)
				throttling->indep_throttle_status = indep;
		}
	} else if (header->format_revision == 3) {
		this->is_apu = true;
		struct gpu_metrics_v3_0 *amdgpu_metrics = (struct gpu_metrics_v3_0 *) buf;

		metrics->gpu_temp_c = amdgpu_metrics->temperature_gfx / 100;
		metrics->soc_temp_c = amdgpu_metrics->temperature_soc / 100;

		uint16_t cpu_temp = 0;

		for (unsigned i = 0; i < ARRAY_SIZE(amdgpu_metrics->temperature_core); i++) {
			if (!IS_VALID_METRIC(amdgpu_metrics->temperature_core[i]))
				break;

			cpu_temp = MAX(cpu_temp, amdgpu_metrics->temperature_core[i]);
		}
		metrics->apu_cpu_temp_c = cpu_temp / 100;

		metrics->gpu_load_percent = amdgpu_metrics->average_gfx_activity;
		// average_apu_power includes gfx_power so remove that from cpu_power
		int64_t apu_power = amdgpu_metrics->average_apu_power;
		int64_t gfx_power = amdgpu_metrics->average_gfx_power;
		metrics->average_cpu_power_w = float(apu_power - gfx_power) / 1000.0;
		if (metrics->average_cpu_power_w < 0) metrics->average_cpu_power_w = 0;
		metrics->average_gfx_power_w = amdgpu_metrics->average_gfx_power / 1000.0;
		metrics->current_gfxclk_mhz = amdgpu_metrics->average_gfxclk_frequency;
		metrics->current_uclk_mhz = amdgpu_metrics->average_uclk_frequency;
		
		if (previous_metrics.common_header.structure_size == 0) {
			previous_metrics = *amdgpu_metrics;
			is_temp = false;
			is_power = false;
			is_current = false;
			is_other = false;
			if (throttling) {
				throttling->use_v3 = true;
				throttling->v3_power.store(false);
				throttling->v3_thermal.store(false);
			}
			return;	
		} else {
			uint32_t d_thm_core = V3_THROTTLING_DELTA(thm_core);
			uint32_t d_thm_gfx  = V3_THROTTLING_DELTA(thm_gfx);
			uint32_t d_thm_soc  = V3_THROTTLING_DELTA(thm_soc);
			uint32_t d_spl      = V3_THROTTLING_DELTA(spl);
			uint32_t d_fppt     = V3_THROTTLING_DELTA(fppt);
			uint32_t d_sppt     = V3_THROTTLING_DELTA(sppt);
			uint32_t d_prochot  = V3_THROTTLING_DELTA(prochot);
			is_temp = (d_thm_core | d_thm_gfx | d_thm_soc | d_prochot) > 0;
			is_power = (d_spl | d_fppt | d_sppt) > 0;
			// there is no current throttling flags in v3_0
			is_current = false;
			// also no "other" throttling in v3_0
			is_other = false;

			previous_metrics = *amdgpu_metrics;

			if (throttling) {
				throttling->use_v3 = true;
				// we only check spl, this attempts to match how we handle it in v1 and v2
				// as close as we can
				throttling->v3_power.store(d_spl > 0);
				throttling->v3_thermal.store(is_temp);
			}
		}
	}

	/* Throttling: See
	https://elixir.bootlin.com/linux/latest/source/drivers/gpu/drm/amd/pm/swsmu/inc/amdgpu_smu.h
	for the offsets */
	metrics->is_power_throttled   = is_power;
	metrics->is_current_throttled = is_current;
	metrics->is_temp_throttled    = is_temp;
	metrics->is_other_throttled   = is_other;
}

void AMDGPU::get_samples_and_copy(struct amdgpu_common_metrics metrics_buffer[METRICS_SAMPLE_COUNT], bool &gpu_load_needs_dividing) {
	while (!stop_thread) {
		// Get all the samples
		for (size_t cur_sample_id=0; cur_sample_id < METRICS_SAMPLE_COUNT; cur_sample_id++) {
			if (gpu_metrics_is_valid)
				get_instant_metrics(&metrics_buffer[cur_sample_id]);

			// Detect and fix if the gpu load is reported in centipercent
			if (gpu_load_needs_dividing || metrics_buffer[cur_sample_id].gpu_load_percent > 100){
				gpu_load_needs_dividing = true;
				metrics_buffer[cur_sample_id].gpu_load_percent /= 100;
			}

			usleep(METRICS_POLLING_PERIOD_MS * 1000);
		}

		if (stop_thread) break;

        std::unique_lock<std::mutex> lock(metrics_mutex);
        cond_var.wait(lock, [this]() { return !paused || stop_thread; });
		// do one pass of metrics from sysfs nodes
		// then we replace with GPU metrics if it's available
		get_sysfs_metrics();

#ifndef TEST_ONLY
		metrics.proc_vram_used = fdinfo_helper->amdgpu_helper_get_proc_vram();
#endif

		if (gpu_metrics_is_valid) {
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

			metrics.fan_speed = amdgpu_common_metrics.fan_speed;
		}
	}
}

void AMDGPU::metrics_polling_thread() {
	struct amdgpu_common_metrics metrics_buffer[METRICS_SAMPLE_COUNT];
	bool gpu_load_needs_dividing = false;  //some GPUs report load as centipercent

	// Initial poll of the metrics, so that we have values to display as fast as possible
	get_instant_metrics(&amdgpu_common_metrics);
	if (amdgpu_common_metrics.gpu_load_percent > 100){
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

void AMDGPU::get_sysfs_metrics() {
    int64_t value = 0;
	if (sysfs_nodes.busy) {
		rewind(sysfs_nodes.busy);
		fflush(sysfs_nodes.busy);
		int value = 0;
		if (fscanf(sysfs_nodes.busy, "%d", &value) != 1)
			value = 0;
		metrics.load = value;
	}

	if (sysfs_nodes.memory_clock) {
		rewind(sysfs_nodes.memory_clock);
		fflush(sysfs_nodes.memory_clock);
		if (fscanf(sysfs_nodes.memory_clock, "%" PRId64, &value) != 1)
			value = 0;

		metrics.MemClock = value / 1000000;
	}

	// TODO: on some gpus this will use the power1_input instead
	// this value is instantaneous and should be averaged over time
	// probably just average everything in this function to be safe
#ifndef TEST_ONLY
	if (get_params()->enabled[OVERLAY_PARAM_ENABLED_gpu_power]) {
		// NOTE: Do not read power1_average if it is not enabled, as some
		// older GPUs may hang when reading the sysfs node.
		metrics.powerUsage = 0;
	} else
#endif
	if (sysfs_nodes.power_usage) {
		rewind(sysfs_nodes.power_usage);
		fflush(sysfs_nodes.power_usage);
		if (fscanf(sysfs_nodes.power_usage, "%" PRId64, &value) != 1)
			value = 0;

		metrics.powerUsage = value / 1000000;
	}

#ifndef TEST_ONLY
	if (!get_params()->enabled[OVERLAY_PARAM_ENABLED_gpu_power_limit]) {
		// NOTE: Do not read power1_cap if it is not enabled, as some
		// older GPUs may hang when reading the sysfs node.
		metrics.powerLimit = 0;
	} else
#endif
	if (sysfs_nodes.power_limit) {
		rewind(sysfs_nodes.power_limit);
		fflush(sysfs_nodes.power_limit);
		if (fscanf(sysfs_nodes.power_limit, "%" PRId64, &value) != 1)
			value = 0;

		metrics.powerLimit = value / 1000000;
	}

	if (sysfs_nodes.fan) {
		rewind(sysfs_nodes.fan);
		fflush(sysfs_nodes.fan);
		if (fscanf(sysfs_nodes.fan, "%" PRId64, &value) != 1)
			value = 0;
		metrics.fan_speed = value;
		metrics.fan_rpm = true;
	}

	if (sysfs_nodes.vram_total) {
		rewind(sysfs_nodes.vram_total);
		fflush(sysfs_nodes.vram_total);
		if (fscanf(sysfs_nodes.vram_total, "%" PRId64, &value) != 1)
			value = 0;
		metrics.memoryTotal = float(value) / (1024 * 1024 * 1024);
	}

	if (sysfs_nodes.vram_used) {
		rewind(sysfs_nodes.vram_used);
		fflush(sysfs_nodes.vram_used);
		if (fscanf(sysfs_nodes.vram_used, "%" PRId64, &value) != 1)
			value = 0;
		metrics.sys_vram_used = float(value) / (1024 * 1024 * 1024);
	}
	// On some GPUs SMU can sometimes return the wrong temperature.
	// As HWMON is way more visible than the SMU metrics, let's always trust it as it is the most likely to work
	if (sysfs_nodes.core_clock) {
		rewind(sysfs_nodes.core_clock);
		fflush(sysfs_nodes.core_clock);
		if (fscanf(sysfs_nodes.core_clock, "%" PRId64, &value) != 1)
			value = 0;

		metrics.CoreClock = value / 1000000;
	}

	if (sysfs_nodes.temp){
		rewind(sysfs_nodes.temp);
		fflush(sysfs_nodes.temp);
		int value = 0;
		if (fscanf(sysfs_nodes.temp, "%d", &value) != 1)
			value = 0;
		metrics.temp = value / 1000;
	}

	if (sysfs_nodes.junction_temp){
		rewind(sysfs_nodes.junction_temp);
		fflush(sysfs_nodes.junction_temp);
		int value = 0;
		if (fscanf(sysfs_nodes.junction_temp, "%d", &value) != 1)
			value = 0;
		metrics.junction_temp = value / 1000;
	}

	if (sysfs_nodes.memory_temp){
		rewind(sysfs_nodes.memory_temp);
		fflush(sysfs_nodes.memory_temp);
		int value = 0;
		if (fscanf(sysfs_nodes.memory_temp, "%d", &value) != 1)
			value = 0;
		metrics.memory_temp = value / 1000;
	}

	if (sysfs_nodes.gtt_used) {
		rewind(sysfs_nodes.gtt_used);
		fflush(sysfs_nodes.gtt_used);
		if (fscanf(sysfs_nodes.gtt_used, "%" PRId64, &value) != 1)
			value = 0;
		metrics.gtt_used = float(value) / (1024 * 1024 * 1024);
	}

	if (sysfs_nodes.gpu_voltage_soc) {
		rewind(sysfs_nodes.gpu_voltage_soc);
		fflush(sysfs_nodes.gpu_voltage_soc);
		if (fscanf(sysfs_nodes.gpu_voltage_soc, "%" PRId64, &value) != 1)
			value = 0;
		metrics.voltage = value;
	}
}

AMDGPU::AMDGPU(std::string pci_dev, uint32_t device_id, uint32_t vendor_id) {
	this->pci_dev = pci_dev;
	this->device_id = device_id;
	this->vendor_id = vendor_id;
	const std::string device_path = "/sys/bus/pci/devices/" + pci_dev;
	gpu_metrics_path = device_path + "/gpu_metrics";
    // Just check that the metrics file exists and is readable
    FILE *f = fopen(gpu_metrics_path.c_str(), "rb");
    if (f) {
        gpu_metrics_is_valid = true;
        fclose(f);
    } else {
        gpu_metrics_is_valid = false;
        SPDLOG_DEBUG("Failed to open gpu_metrics at '{}'", gpu_metrics_path);
    }

	sysfs_nodes.busy = fopen((device_path + "/gpu_busy_percent").c_str(), "r");
	sysfs_nodes.vram_total = fopen((device_path + "/mem_info_vram_total").c_str(), "r");
	sysfs_nodes.vram_used = fopen((device_path + "/mem_info_vram_used").c_str(), "r");
	sysfs_nodes.gtt_used = fopen((device_path + "/mem_info_gtt_used").c_str(), "r");

	const std::string hwmon_path = device_path + "/hwmon/";
	if (fs::exists(hwmon_path)){
		const auto dirs = ls(hwmon_path.c_str(), "hwmon", LS_DIRS);
		for (const auto& dir : dirs) {
			sysfs_nodes.temp = fopen((hwmon_path + dir + "/temp1_input").c_str(), "r");
			sysfs_nodes.junction_temp = fopen((hwmon_path + dir + "/temp2_input").c_str(), "r");
			sysfs_nodes.memory_temp = fopen((hwmon_path + dir + "/temp3_input").c_str(), "r");
			sysfs_nodes.core_clock = fopen((hwmon_path + dir + "/freq1_input").c_str(), "r");
			sysfs_nodes.gpu_voltage_soc = fopen((hwmon_path + dir + "/in0_input").c_str(), "r");
			sysfs_nodes.memory_clock = fopen((hwmon_path + dir + "/freq2_input").c_str(), "r");

			for (std::string p : { "power1_average", "power1_input" }) {
				std::string sensor = hwmon_path + dir + "/" + p;

				if (!fs::exists(sensor))
					continue;

				sysfs_nodes.power_usage = fopen(sensor.c_str(), "r");
				break;
			}

			sysfs_nodes.power_limit = fopen((hwmon_path + dir + "/power1_cap").c_str(), "r");
			sysfs_nodes.fan = fopen((hwmon_path + dir + "/fan1_input").c_str(), "r");
		}
	}

	throttling = std::make_shared<Throttling>(0x1002);
#ifndef TEST_ONLY
	fdinfo_helper = std::make_unique<GPU_fdinfo>("amdgpu", pci_dev, "", /*called_from_amdgpu_cpp=*/ true);
#endif

	thread = std::thread(&AMDGPU::metrics_polling_thread, this);
	pthread_setname_np(thread.native_handle(), "mangohud-amdgpu");
}
