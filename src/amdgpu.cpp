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
#include <endian.h>

std::string metrics_path = "";
struct amdgpu_common_metrics amdgpu_common_metrics;
std::mutex amdgpu_common_metrics_m;
std::mutex amdgpu_m;
std::condition_variable amdgpu_c;
bool amdgpu_run_thread = true;
std::unique_ptr<Throttling> throttling;

bool amdgpu_verify_metrics(const std::string& path)
{
	metrics_table_header header {};
	FILE *f;
	f = fopen(path.c_str(), "rb");
	if (!f) {
		SPDLOG_DEBUG("Failed to read the metrics header of '{}'", path);
		return false;
	}

	if (fread(&header, sizeof(header), 1, f) == 0)
	{
		SPDLOG_DEBUG("Failed to read the metrics header of '{}'", path);
		return false;
	}

	switch (header.format_revision)
	{
		case 1: // v1_1, v1_2, v1_3
			if(header.content_revision<=0 || header.content_revision>3)// v1_0, not naturally aligned
				break;
			cpuStats.cpu_type = "GPU";
			return true;
		case 2: // v2_1, v2_2, v2_3, v2_4
			if(header.content_revision<=0 || header.content_revision>4)// v2_0, not naturally aligned
				break;
			cpuStats.cpu_type = "APU";
			return true;
		default:
			break;
	}

	SPDLOG_WARN("Unsupported gpu_metrics version: {}.{}", header.format_revision, header.content_revision);
	return false;
}

#define IS_VALID_METRIC(FIELD) (FIELD != 0xffff)
void amdgpu_get_instant_metrics(struct amdgpu_common_metrics *metrics) {
	FILE *f;
	void *buf[MAX(sizeof(struct gpu_metrics_v1_3), sizeof(struct gpu_metrics_v2_4))/sizeof(void*)+1];
	struct metrics_table_header* header = (metrics_table_header*)buf;

	f = fopen(metrics_path.c_str(), "rb");
	if (!f)
		return;

	// Read the whole file
	if (fread(buf, sizeof(buf), 1, f) != 0) {
		SPDLOG_DEBUG("amdgpu metrics file '{}' is larger than the buffer", metrics_path.c_str());
		fclose(f);
		return;
	}
	fclose(f);

	int64_t indep_throttle_status = 0;
	if (header->format_revision == 1) {
		// Desktop GPUs
		struct gpu_metrics_v1_3 *amdgpu_metrics = (struct gpu_metrics_v1_3 *) buf;
		metrics->gpu_load_percent = le16toh(amdgpu_metrics->average_gfx_activity);

		metrics->average_gfx_power_w = le16toh(amdgpu_metrics->average_socket_power);

		metrics->current_gfxclk_mhz = le16toh(amdgpu_metrics->current_gfxclk);
		metrics->current_uclk_mhz = le16toh(amdgpu_metrics->current_uclk);

		metrics->gpu_temp_c = le16toh(amdgpu_metrics->temperature_edge);
		indep_throttle_status = le16toh(amdgpu_metrics->indep_throttle_status);
		metrics->fan_speed = le16toh(amdgpu_metrics->current_fan_speed);
	} else if (header->format_revision == 2) {
		// APUs
		struct gpu_metrics_v2_3 *amdgpu_metrics = (struct gpu_metrics_v2_3 *) buf;

		metrics->gpu_load_percent = le16toh(amdgpu_metrics->average_gfx_activity);

		metrics->average_gfx_power_w = le16toh(amdgpu_metrics->average_gfx_power) / 1000.f;

		if( IS_VALID_METRIC(amdgpu_metrics->average_cpu_power) ) {
			// prefered method
			metrics->average_cpu_power_w = le16toh(amdgpu_metrics->average_cpu_power) / 1000.f;
		} else if( IS_VALID_METRIC(amdgpu_metrics->average_core_power[0]) ) {
			// fallback 1: sum of core power
			metrics->average_cpu_power_w = 0;
			unsigned i = 0;
			do metrics->average_cpu_power_w = metrics->average_cpu_power_w + le16toh(amdgpu_metrics->average_core_power[i]) / 1000.f;
			while (++i < ARRAY_SIZE(amdgpu_metrics->average_core_power) && IS_VALID_METRIC(amdgpu_metrics->average_core_power[i]));
		} else if( IS_VALID_METRIC(amdgpu_metrics->average_socket_power) && IS_VALID_METRIC(amdgpu_metrics->average_gfx_power) ) {
			// fallback 2: estimate cpu power from total socket power
			metrics->average_cpu_power_w = le16toh(amdgpu_metrics->average_socket_power) / 1000.f - le16toh(amdgpu_metrics->average_gfx_power) / 1000.f;
		} else {
			// giving up
			metrics->average_cpu_power_w = 0;
		}

		if( IS_VALID_METRIC(amdgpu_metrics->current_gfxclk) ) {
			// prefered method
			metrics->current_gfxclk_mhz = le16toh(amdgpu_metrics->current_gfxclk);
		} else if( IS_VALID_METRIC(amdgpu_metrics->average_gfxclk_frequency) ) {
			// fallback 1
			metrics->current_gfxclk_mhz = le16toh(amdgpu_metrics->average_gfxclk_frequency);
		} else {
			// giving up
			metrics->current_gfxclk_mhz = 0;
		}
		if( IS_VALID_METRIC(amdgpu_metrics->current_uclk) ) {
			// prefered method
			metrics->current_uclk_mhz = le16toh(amdgpu_metrics->current_uclk);
		} else if( IS_VALID_METRIC(amdgpu_metrics->average_uclk_frequency) ) {
			// fallback 1
			metrics->current_uclk_mhz = le16toh(amdgpu_metrics->average_uclk_frequency);
		} else {
			// giving up
			metrics->current_uclk_mhz = 0;
		}

		if( IS_VALID_METRIC(amdgpu_metrics->temperature_soc) ) {
			// prefered method
			metrics->soc_temp_c = le16toh(amdgpu_metrics->temperature_soc) / 100;
		} else if( header->content_revision >= 3 && IS_VALID_METRIC(amdgpu_metrics->average_temperature_soc) ) {
			// fallback 1
			metrics->soc_temp_c = le16toh(amdgpu_metrics->average_temperature_soc) / 100;
		} else {
			// giving up
			metrics->soc_temp_c = 0;
		}
		if( IS_VALID_METRIC(amdgpu_metrics->temperature_gfx) ) {
			// prefered method
			metrics->gpu_temp_c = le16toh(amdgpu_metrics->temperature_gfx) / 100;
		} else if( header->content_revision >= 3 && IS_VALID_METRIC(amdgpu_metrics->average_temperature_gfx) ) {
			// fallback 1
			metrics->gpu_temp_c = le16toh(amdgpu_metrics->average_temperature_gfx) / 100;
		} else {
			// giving up
			metrics->gpu_temp_c = 0;
		}

		int cpu_temp = 0;
		if( IS_VALID_METRIC(amdgpu_metrics->temperature_core[0]) ) {
			// prefered method
			uint64_t cpu_temp = 0;
			for (size_t i = 0; i < ARRAY_SIZE(amdgpu_metrics->temperature_core); i++)
				if (IS_VALID_METRIC(amdgpu_metrics->temperature_core[i]))
					if (cpu_temp < amdgpu_metrics->temperature_core[i])
						cpu_temp = amdgpu_metrics->temperature_core[i];

			metrics->apu_cpu_temp_c = le16toh(cpu_temp) / 100;
			
		} else if( header->content_revision >= 3 && IS_VALID_METRIC(amdgpu_metrics->average_temperature_core[0]) ) {
			// fallback 1
			uint64_t cpu_temp = 0;
			for (size_t i = 0; i < ARRAY_SIZE(amdgpu_metrics->average_temperature_core); i++)
				if (IS_VALID_METRIC(amdgpu_metrics->average_temperature_core[i]))
					if (cpu_temp < amdgpu_metrics->average_temperature_core[i])
						cpu_temp = amdgpu_metrics->average_temperature_core[i];

			metrics->apu_cpu_temp_c = le16toh(cpu_temp) / 100;
		} else if( cpuStats.ReadcpuTempFile(cpu_temp) ) {
			// fallback 2: Try temp from file 'm_cpuTempFile' of 'cpu.cpp'
			metrics->apu_cpu_temp_c = cpu_temp;
		} else {
			// giving up
			metrics->apu_cpu_temp_c = 0;
		}

		indep_throttle_status = amdgpu_metrics->indep_throttle_status;
	}

	/* Throttling: See
	https://elixir.bootlin.com/linux/latest/source/drivers/gpu/drm/amd/pm/swsmu/inc/amdgpu_smu.h
	for the offsets */
	metrics->is_power_throttled = le16toh(((indep_throttle_status) >> 0) & 0xFF) != 0;
	metrics->is_current_throttled = le16toh(((indep_throttle_status) >> 16) & 0xFF) != 0;
	metrics->is_temp_throttled = le16toh(((indep_throttle_status) >> 32) & 0xFFFF) != 0;
	metrics->is_other_throttled = le16toh(((indep_throttle_status) >> 56) & 0xFF) != 0;
	if (throttling)
		throttling->indep_throttle_status = indep_throttle_status;
}

void amdgpu_get_samples_and_copy(struct amdgpu_common_metrics metrics_buffer[METRICS_SAMPLE_COUNT], bool &gpu_load_needs_dividing) {
		// Get all the samples
		for (size_t cur_sample_id=0; cur_sample_id < METRICS_SAMPLE_COUNT; cur_sample_id++) {
			amdgpu_get_instant_metrics(&metrics_buffer[cur_sample_id]);

			// Detect and fix if the gpu load is reported in centipercent
			if (gpu_load_needs_dividing || metrics_buffer[cur_sample_id].gpu_load_percent > 100){
				gpu_load_needs_dividing = true;
				metrics_buffer[cur_sample_id].gpu_load_percent /= 100;
			}

			usleep(METRICS_POLLING_PERIOD_MS * 1000);
		}

		// Copy the results from the different metrics to amdgpu_common_metrics
		amdgpu_common_metrics_m.lock();
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
		amdgpu_common_metrics_m.unlock();
}

void amdgpu_metrics_polling_thread() {
	struct amdgpu_common_metrics metrics_buffer[METRICS_SAMPLE_COUNT];
	bool gpu_load_needs_dividing = false;  //some GPUs report load as centipercent

	// Initial poll of the metrics, so that we have values to display as fast as possible
	amdgpu_get_instant_metrics(&amdgpu_common_metrics);
	if (amdgpu_common_metrics.gpu_load_percent > 100){
		gpu_load_needs_dividing = true;
		amdgpu_common_metrics.gpu_load_percent /= 100;
	}

	// Set all the fields to 0 by default. Only done once as we're just replacing previous values after
	memset(metrics_buffer, 0, sizeof(metrics_buffer));

	while (1) {
		std::unique_lock<std::mutex> lock(amdgpu_m);
		amdgpu_c.wait(lock, []{return amdgpu_run_thread;});
		lock.unlock();
#ifndef TEST_ONLY
		if (HUDElements.params->no_display && !logger->is_active())
			usleep(100000);
		else
#endif
			amdgpu_get_samples_and_copy(metrics_buffer, gpu_load_needs_dividing);
	}
}

void amdgpu_get_metrics(uint32_t deviceID){
	static bool init = false;
	if (!init){
		std::thread(amdgpu_metrics_polling_thread).detach();
		init = true;
	}

	amdgpu_common_metrics_m.lock();
	gpu_info.load = amdgpu_common_metrics.gpu_load_percent;

	gpu_info.powerUsage = amdgpu_common_metrics.average_gfx_power_w;
	gpu_info.MemClock = amdgpu_common_metrics.current_uclk_mhz;

	// Use hwmon instead, see gpu.cpp
	if ( deviceID == 0x1435 || deviceID == 0x163f )
	{
		// If we are on VANGOGH (Steam Deck), then
		// always use use core clock from GPU metrics.
		gpu_info.CoreClock = amdgpu_common_metrics.current_gfxclk_mhz;
	}
	// gpu_info.temp = amdgpu_common_metrics.gpu_temp_c;
	gpu_info.apu_cpu_power = amdgpu_common_metrics.average_cpu_power_w;
	gpu_info.apu_cpu_temp = amdgpu_common_metrics.apu_cpu_temp_c;

	gpu_info.is_power_throttled = amdgpu_common_metrics.is_power_throttled;
	gpu_info.is_current_throttled = amdgpu_common_metrics.is_current_throttled;
	gpu_info.is_temp_throttled = amdgpu_common_metrics.is_temp_throttled;
	gpu_info.is_other_throttled = amdgpu_common_metrics.is_other_throttled;

	gpu_info.fan_speed = amdgpu_common_metrics.fan_speed;

	amdgpu_common_metrics_m.unlock();
}
