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

std::string metrics_path = "";
struct amdgpu_common_metrics amdgpu_common_metrics;
std::mutex amdgpu_common_metrics_m;

bool amdgpu_verify_metrics(const std::string& path)
{
    metrics_table_header header {};
	FILE *f;
	f = fopen(path.c_str(), "rb");
	if (!f)
		return false;

    if (fread(&header, sizeof(header), 1, f) == 0)
    {
        SPDLOG_DEBUG("Failed to read the metrics header of '{}'", path);
        return false;
    }

    switch (header.structure_size)
    {
        case 80: // v1_0, not naturally aligned
			return false;
        case 96: // v1_1
        case 104: // v1_2
        case sizeof(gpu_metrics_v1_3): // v2.0, v2.1
        case sizeof(gpu_metrics_v2_2):
            if (header.format_revision == 1 || header.format_revision == 2)
                return true;
        default:
            break;
    }

    SPDLOG_WARN("Unsupported gpu_metrics version: {}.{}", header.format_revision, header.content_revision);
    return false;
}

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
void amdgpu_get_instant_metrics(struct amdgpu_common_metrics *metrics) {
	FILE *f;
	void *buf[MAX(sizeof(struct gpu_metrics_v1_3), sizeof(struct gpu_metrics_v2_2))];
	struct metrics_table_header* header = (metrics_table_header*)buf;

	f = fopen(metrics_path.c_str(), "rb");
	if (!f)
		return;

	// Read the whole file
	if (!fread(buf, sizeof(buf), 1, f) == 0) {
		SPDLOG_DEBUG("Failed to read amdgpu metrics file '{}'", metrics_path.c_str());
		fclose(f);
		return;
	}
	fclose(f);

	int64_t indep_throttle_status = 0;
	if (header->format_revision == 1) {
		// Desktop GPUs
		cpuStats.cpu_type = "GPU";
		struct gpu_metrics_v1_3 *amdgpu_metrics = (struct gpu_metrics_v1_3 *) buf;
		metrics->gpu_load_percent = amdgpu_metrics->average_gfx_activity;

		metrics->average_gfx_power_w = amdgpu_metrics->average_socket_power;

		metrics->current_gfxclk_mhz = amdgpu_metrics->current_gfxclk;
		metrics->current_uclk_mhz = amdgpu_metrics->current_uclk;

		metrics->gpu_temp_c = amdgpu_metrics->temperature_edge;
		indep_throttle_status = amdgpu_metrics->indep_throttle_status;
	} else if (header->format_revision == 2) {
		// APUs
		cpuStats.cpu_type = "APU";
		struct gpu_metrics_v2_2 *amdgpu_metrics = (struct gpu_metrics_v2_2 *) buf;

		metrics->gpu_load_percent = amdgpu_metrics->average_gfx_activity;

		metrics->average_gfx_power_w = amdgpu_metrics->average_gfx_power / 1000.f;
		metrics->average_cpu_power_w = amdgpu_metrics->average_cpu_power / 1000.f;

		metrics->current_gfxclk_mhz = amdgpu_metrics->current_gfxclk;
		metrics->current_uclk_mhz = amdgpu_metrics->current_uclk;

		metrics->soc_temp_c = amdgpu_metrics->temperature_soc / 100;
		metrics->gpu_temp_c = amdgpu_metrics->temperature_gfx / 100;
		int cpu_temp = 0;
		for (unsigned i = 0; i < cpuStats.GetCPUData().size() / 2; i++)
			cpu_temp = MAX(cpu_temp, amdgpu_metrics->temperature_core[i]);
		metrics->apu_cpu_temp_c = cpu_temp / 100;
		indep_throttle_status = amdgpu_metrics->indep_throttle_status;
	}

	/* Throttling: See
	https://elixir.bootlin.com/linux/latest/source/drivers/gpu/drm/amd/pm/inc/amdgpu_smu.h
	for the offsets */
	metrics->is_power_throttled = ((indep_throttle_status >> 0) & 0xFF) != 0;
	metrics->is_current_throttled = ((indep_throttle_status >> 16) & 0xFF) != 0;
	metrics->is_temp_throttled = ((indep_throttle_status >> 32) & 0xFFFF) != 0;
	metrics->is_other_throttled = ((indep_throttle_status >> 56) & 0xFF) != 0;
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
#ifndef TEST_ONLY
		if (HUDElements.params->no_display && !logger->is_active())
			usleep(100000);
		else
#endif
			amdgpu_get_samples_and_copy(metrics_buffer, gpu_load_needs_dividing);
	}
}

void amdgpu_get_metrics(){
	static bool init = false;
	if (!init){
		std::thread(amdgpu_metrics_polling_thread).detach();
		init = true;
	}

	amdgpu_common_metrics_m.lock();
	gpu_info.load = amdgpu_common_metrics.gpu_load_percent;

	gpu_info.powerUsage = amdgpu_common_metrics.average_gfx_power_w;
	gpu_info.CoreClock = amdgpu_common_metrics.current_gfxclk_mhz;
	gpu_info.MemClock = amdgpu_common_metrics.current_uclk_mhz;

	// Use hwmon instead, see gpu.cpp
	// gpu_info.temp = amdgpu_common_metrics.gpu_temp_c;
	gpu_info.apu_cpu_power = amdgpu_common_metrics.average_cpu_power_w;
	gpu_info.apu_cpu_temp = amdgpu_common_metrics.apu_cpu_temp_c;

	gpu_info.is_power_throttled = amdgpu_common_metrics.is_power_throttled;
	gpu_info.is_current_throttled = amdgpu_common_metrics.is_current_throttled;
	gpu_info.is_temp_throttled = amdgpu_common_metrics.is_temp_throttled;
	gpu_info.is_other_throttled = amdgpu_common_metrics.is_other_throttled;

	amdgpu_common_metrics_m.unlock();
}
