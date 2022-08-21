#include <spdlog/spdlog.h>
#include <thread>
#include "amdgpu.h"
#include "gpu.h"
#include "cpu.h"
#include "overlay.h"

#ifdef USE_SSE2
#include <emmintrin.h>
#endif

#define METRICS_UPDATE_PERIOD_MS 500
#define METRICS_POLLING_PERIOD_MS 5
#define METRICS_SAMPLE_COUNT (METRICS_UPDATE_PERIOD_MS/METRICS_POLLING_PERIOD_MS)

std::string metrics_path = "";

/* This structure is used to communicate the latest values of the amdgpu metrics.
 * The direction of communication is amdgpu_polling_thread -> amdgpu_get_metrics().
 */
struct amdgpu_common_metrics {
	/* Load level: averaged across the sampling period */
	uint16_t gpu_load_percent[METRICS_SAMPLE_COUNT];
	// uint16_t mem_load_percent;

	/* Power usage: averaged across the sampling period */
	float average_gfx_power_w[METRICS_SAMPLE_COUNT];
	float average_cpu_power_w[METRICS_SAMPLE_COUNT];

	/* Clocks: latest value of the clock */
	uint16_t current_gfxclk_mhz[METRICS_SAMPLE_COUNT];
	uint16_t current_uclk_mhz[METRICS_SAMPLE_COUNT];

	/* Temperatures: maximum values over the sampling period */
#ifdef AMG_GPU_TEMP_MONITORING
	uint16_t soc_temp_c[METRICS_SAMPLE_COUNT];
#endif
	uint16_t gpu_temp_c[METRICS_SAMPLE_COUNT];
	uint16_t apu_cpu_temp_c[METRICS_SAMPLE_COUNT];

	/* throttling status */
	bool is_power_throttled[METRICS_SAMPLE_COUNT];
	bool is_current_throttled[METRICS_SAMPLE_COUNT];
	bool is_temp_throttled[METRICS_SAMPLE_COUNT];
	bool is_other_throttled[METRICS_SAMPLE_COUNT];
} amdgpu_common_metrics;

std::mutex amdgpu_common_metrics_m;

bool amdgpu_check_metrics(const std::string& path)
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
void amdgpu_get_instant_metrics(struct amdgpu_common_metrics *metrics, size_t index) {
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
		metrics->gpu_load_percent[index] = amdgpu_metrics->average_gfx_activity;

		metrics->average_gfx_power_w[index] = amdgpu_metrics->average_socket_power;

		metrics->current_gfxclk_mhz[index] = amdgpu_metrics->current_gfxclk;
		metrics->current_uclk_mhz[index] = amdgpu_metrics->current_uclk;

		metrics->gpu_temp_c[index] = amdgpu_metrics->temperature_edge;
		indep_throttle_status = amdgpu_metrics->indep_throttle_status;
	} else if (header->format_revision == 2) {
		// APUs
		cpuStats.cpu_type = "APU";
		struct gpu_metrics_v2_2 *amdgpu_metrics = (struct gpu_metrics_v2_2 *) buf;

		metrics->gpu_load_percent[index] = amdgpu_metrics->average_gfx_activity;

		metrics->average_gfx_power_w[index] = amdgpu_metrics->average_gfx_power / 1000.f;
		metrics->average_cpu_power_w[index] = amdgpu_metrics->average_cpu_power / 1000.f;

		metrics->current_gfxclk_mhz[index] = amdgpu_metrics->current_gfxclk;
		metrics->current_uclk_mhz[index] = amdgpu_metrics->current_uclk;

#ifdef AMD_GPU_MONITORING
		metrics->soc_temp_c[index] = amdgpu_metrics->temperature_soc / 100;
#endif
		metrics->gpu_temp_c[index] = amdgpu_metrics->temperature_gfx / 100;
		int cpu_temp = 0;
		for (unsigned i = 0; i < cpuStats.GetCPUData().size() / 2; i++)
			cpu_temp = MAX(cpu_temp, amdgpu_metrics->temperature_core[i]);
		metrics->apu_cpu_temp_c[index] = cpu_temp / 100;
		indep_throttle_status = amdgpu_metrics->indep_throttle_status;
	}

	/* Throttling: See
	https://elixir.bootlin.com/linux/latest/source/drivers/gpu/drm/amd/pm/inc/amdgpu_smu.h
	for the offsets */
	metrics->is_power_throttled[index] = ((indep_throttle_status >> 0) & 0xFF) != 0;
	metrics->is_current_throttled[index] = ((indep_throttle_status >> 16) & 0xFF) != 0;
	metrics->is_temp_throttled[index] = ((indep_throttle_status >> 32) & 0xFFFF) != 0;
	metrics->is_other_throttled[index] = ((indep_throttle_status >> 56) & 0xFF) != 0;
}

#define UPDATE_METRIC_AVERAGE(FIELD,GPU_INFO_FIELD) do { int value_sum = 0; for (size_t s=0; s < METRICS_SAMPLE_COUNT; s++) { value_sum += metrics_buffer.FIELD[s]; } scratchInfo.GPU_INFO_FIELD = value_sum / METRICS_SAMPLE_COUNT; } while(0)

#ifdef USE_SSE2
  static_assert(METRICS_SAMPLE_COUNT % 4 == 0, "SSE2 support requires a multiple of 4 samples");
  #define UPDATE_METRIC_AVERAGE_FLOAT(FIELD,GPU_INFO_FIELD) do { \
    __m128 value_sum = _mm_set1_ps(0.0f); /* initialize the accumulator */ \
    for (size_t s = 0; s < METRICS_SAMPLE_COUNT; s+=4) { \
      __m128 v = _mm_load_ps(&metrics_buffer.FIELD[s]); /* load 4 values in to add */ \
      value_sum = _mm_add_ps(value_sum, v); /* add the values */ \
    } \
    float sum[4]; /* scratch to add the values horizontally */ \
    _mm_store_ps(sum, value_sum); \
    /* no horizontal add until SSE 3, so just accumulate the last results in a loop */ \
    for (size_t s = 1; s < 4; s++) { \
      sum[0] += sum[1]; \
    } \
    scratchInfo.GPU_INFO_FIELD = sum[0] / METRICS_SAMPLE_COUNT; \
  } while(0)
#else
  #define UPDATE_METRIC_AVERAGE_FLOAT(FIELD,GPU_INFO_FIELD) do { float value_sum = 0; for (size_t s=0; s < METRICS_SAMPLE_COUNT; s++) { value_sum += metrics_buffer.FIELD[s]; } scratchInfo.GPU_INFO_FIELD = value_sum / METRICS_SAMPLE_COUNT; } while(0)
#endif

#define UPDATE_METRIC_MAX(FIELD,GPU_INFO_FIELD) do { int cur_max = metrics_buffer.FIELD[0]; for (size_t s=1; s < METRICS_SAMPLE_COUNT; s++) { cur_max = MAX(cur_max, metrics_buffer.FIELD[s]); }; scratchInfo.GPU_INFO_FIELD = cur_max; } while(0)

#ifdef USE_SSE2
  #define UPDATE_METRIC_BOOL_SET(FIELD,GPU_INFO_FIELD) do { \
    __m128i accumulator = _mm_set1_epi8(0); \
    size_t s; \
    for (s = 0; s < METRICS_SAMPLE_COUNT - 16; s += 16) { \
      __m128i v = _mm_load_si128((__m128i *)&metrics_buffer.FIELD[s]); \
      accumulator = _mm_or_si128(accumulator, v); \
    }; \
    bool cumulative_results[16]; \
    _mm_store_si128((__m128i *)cumulative_results, accumulator); \
    bool result = false; \
    for (size_t i = 0; i < 16; i++) { \
      result |= cumulative_results[i]; \
    } \
    for ( ; s < METRICS_SAMPLE_COUNT; s++) { \
      result |= metrics_buffer.FIELD[s]; \
    } \
    scratchInfo.GPU_INFO_FIELD = result; \
  } while(0)
#else
  #define UPDATE_METRIC_BOOL_SET(FIELD,GPU_INFO_FIELD) UPDATE_METRIC_MAX(FIELD,GPU_INFO_FIELD)
#endif

static struct gpuInfo scratchInfo = {};

void amdgpu_metrics_polling_thread() {
	struct amdgpu_common_metrics metrics_buffer;
	bool gpu_load_needs_dividing = false;  //some GPUs report load as centipercent

	// Set all the fields to 0 by default. Only done once as we're just replacing previous values after
	memset(&metrics_buffer, 0, sizeof(metrics_buffer));

	// Initial poll of the metrics, so that we have values to display as fast as possible
	amdgpu_get_instant_metrics(&metrics_buffer, 0);

	if (metrics_buffer.gpu_load_percent[0] > 100){
		gpu_load_needs_dividing = true;
		metrics_buffer.gpu_load_percent[0] /= 100;
	}

	while (1) {
		// Get all the samples
		for (size_t cur_sample_id=0; cur_sample_id < METRICS_SAMPLE_COUNT; cur_sample_id++) {
			amdgpu_get_instant_metrics(&metrics_buffer, cur_sample_id);

			// Detect and fix if the gpu load is reported in centipercent
			if (gpu_load_needs_dividing || metrics_buffer.gpu_load_percent[cur_sample_id] > 100){
				gpu_load_needs_dividing = true;
				metrics_buffer.gpu_load_percent[cur_sample_id] /= 100;
			}

			usleep(METRICS_POLLING_PERIOD_MS * 1000);
		}

		// Copy the results from the different metrics to amdgpu_common_metrics
		amdgpu_common_metrics_m.lock();
		UPDATE_METRIC_AVERAGE(gpu_load_percent, load);
		UPDATE_METRIC_AVERAGE_FLOAT(average_gfx_power_w, powerUsage);
		UPDATE_METRIC_AVERAGE_FLOAT(average_cpu_power_w, apu_cpu_power);

		UPDATE_METRIC_AVERAGE(current_gfxclk_mhz, CoreClock);
		UPDATE_METRIC_AVERAGE(current_uclk_mhz, MemClock);

#ifdef AMD_GPU_MONITORING
		UPDATE_METRIC_MAX(soc_temp_c); // FIXME: unusued
#endif
		// Use hwmon instead, see gpu.cpp
		// UPDATE_METRIC_MAX(gpu_temp_c, temp);
		UPDATE_METRIC_MAX(apu_cpu_temp_c, apu_cpu_temp);
		UPDATE_METRIC_BOOL_SET(is_power_throttled, is_power_throttled);
		UPDATE_METRIC_BOOL_SET(is_current_throttled, is_current_throttled);
		UPDATE_METRIC_BOOL_SET(is_temp_throttled, is_temp_throttled);
		UPDATE_METRIC_BOOL_SET(is_other_throttled, is_other_throttled);
		amdgpu_common_metrics_m.unlock();
	}
}

void amdgpu_get_metrics(){
	static bool init = false;
	if (!init){
		std::thread(amdgpu_metrics_polling_thread).detach();
		init = true;
	}

	amdgpu_common_metrics_m.lock();

  memcpy(&gpu_info, &scratchInfo, sizeof (struct gpuInfo));

	amdgpu_common_metrics_m.unlock();
}
