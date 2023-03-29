#pragma once
// #include <fstream>
// #include <iostream>
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <string>
#include "overlay_params.h"
// #include <vector>

#define METRICS_UPDATE_PERIOD_MS 500
#define METRICS_POLLING_PERIOD_MS 5
#define METRICS_SAMPLE_COUNT (METRICS_UPDATE_PERIOD_MS/METRICS_POLLING_PERIOD_MS)
#define NUM_HBM_INSTANCES 4

#define UPDATE_METRIC_AVERAGE(FIELD) do { int value_sum = 0; for (size_t s=0; s < METRICS_SAMPLE_COUNT; s++) { value_sum += metrics_buffer[s].FIELD; } amdgpu_common_metrics.FIELD = value_sum / METRICS_SAMPLE_COUNT; } while(0)
#define UPDATE_METRIC_AVERAGE_FLOAT(FIELD) do { float value_sum = 0; for (size_t s=0; s < METRICS_SAMPLE_COUNT; s++) { value_sum += metrics_buffer[s].FIELD; } amdgpu_common_metrics.FIELD = value_sum / METRICS_SAMPLE_COUNT; } while(0)
#define UPDATE_METRIC_MAX(FIELD) do { int cur_max = metrics_buffer[0].FIELD; for (size_t s=1; s < METRICS_SAMPLE_COUNT; s++) { cur_max = MAX(cur_max, metrics_buffer[s].FIELD); }; amdgpu_common_metrics.FIELD = cur_max; } while(0)
#define UPDATE_METRIC_LAST(FIELD) do { amdgpu_common_metrics.FIELD = metrics_buffer[METRICS_SAMPLE_COUNT - 1].FIELD; } while(0)

struct metrics_table_header {
	uint16_t			structure_size;
	uint8_t				format_revision;
	uint8_t				content_revision;
};

struct gpu_metrics_v1_3 {
	struct metrics_table_header	common_header;

	/* Temperature */
	uint16_t			temperature_edge;
	uint16_t			temperature_hotspot;
	uint16_t			temperature_mem;
	uint16_t			temperature_vrgfx;
	uint16_t			temperature_vrsoc;
	uint16_t			temperature_vrmem;

	/* Utilization */
	uint16_t			average_gfx_activity;
	uint16_t			average_umc_activity; // memory controller
	uint16_t			average_mm_activity; // UVD or VCN

	/* Power/Energy */
	uint16_t			average_socket_power;
	uint64_t			energy_accumulator;

	/* Driver attached timestamp (in ns) */
	uint64_t			system_clock_counter;

	/* Average clocks */
	uint16_t			average_gfxclk_frequency;
	uint16_t			average_socclk_frequency;
	uint16_t			average_uclk_frequency;
	uint16_t			average_vclk0_frequency;
	uint16_t			average_dclk0_frequency;
	uint16_t			average_vclk1_frequency;
	uint16_t			average_dclk1_frequency;

	/* Current clocks */
	uint16_t			current_gfxclk;
	uint16_t			current_socclk;
	uint16_t			current_uclk;
	uint16_t			current_vclk0;
	uint16_t			current_dclk0;
	uint16_t			current_vclk1;
	uint16_t			current_dclk1;

	/* Throttle status */
	uint32_t			throttle_status;

	/* Fans */
	uint16_t			current_fan_speed;

	/* Link width/speed */
	uint16_t			pcie_link_width;
	uint16_t			pcie_link_speed; // in 0.1 GT/s

	uint16_t			padding;

	uint32_t			gfx_activity_acc;
	uint32_t			mem_activity_acc;

	uint16_t			temperature_hbm[NUM_HBM_INSTANCES];

	/* PMFW attached timestamp (10ns resolution) */
	uint64_t			firmware_timestamp;

	/* Voltage (mV) */
	uint16_t			voltage_soc;
	uint16_t			voltage_gfx;
	uint16_t			voltage_mem;

	uint16_t			padding1;

	/* Throttle status (ASIC independent) */
	uint64_t			indep_throttle_status;
};

struct gpu_metrics_v2_2 {
	struct metrics_table_header	common_header;

	/* Temperature */
	uint16_t			temperature_gfx; // gfx temperature on APUs
	uint16_t			temperature_soc; // soc temperature on APUs
	uint16_t			temperature_core[8]; // CPU core temperature on APUs
	uint16_t			temperature_l3[2];

	/* Utilization */
	uint16_t			average_gfx_activity;
	uint16_t			average_mm_activity; // UVD or VCN

	/* Driver attached timestamp (in ns) */
	uint64_t			system_clock_counter;

	/* Power/Energy */
	uint16_t			average_socket_power; // dGPU + APU power on A + A platform
	uint16_t			average_cpu_power;
	uint16_t			average_soc_power;
	uint16_t			average_gfx_power;
	uint16_t			average_core_power[8]; // CPU core power on APUs

	/* Average clocks */
	uint16_t			average_gfxclk_frequency;
	uint16_t			average_socclk_frequency;
	uint16_t			average_uclk_frequency;
	uint16_t			average_fclk_frequency;
	uint16_t			average_vclk_frequency;
	uint16_t			average_dclk_frequency;

	/* Current clocks */
	uint16_t			current_gfxclk;
	uint16_t			current_socclk;
	uint16_t			current_uclk;
	uint16_t			current_fclk;
	uint16_t			current_vclk;
	uint16_t			current_dclk;
	uint16_t			current_coreclk[8]; // CPU core clocks
	uint16_t			current_l3clk[2];

	/* Throttle status (ASIC dependent) */
	uint32_t			throttle_status;

	/* Fans */
	uint16_t			fan_pwm;

	uint16_t			padding[3];

	/* Throttle status (ASIC independent) */
	uint64_t			indep_throttle_status;
};

struct gpu_metrics_v2_3 {
	struct metrics_table_header	common_header;

	/* Temperature */
	uint16_t			temperature_gfx; // gfx temperature on APUs
	uint16_t			temperature_soc; // soc temperature on APUs
	uint16_t			temperature_core[8]; // CPU core temperature on APUs
	uint16_t			temperature_l3[2];

	/* Utilization */
	uint16_t			average_gfx_activity;
	uint16_t			average_mm_activity; // UVD or VCN

	/* Driver attached timestamp (in ns) */
	uint64_t			system_clock_counter;

	/* Power/Energy */
	uint16_t			average_socket_power; // dGPU + APU power on A + A platform
	uint16_t			average_cpu_power;
	uint16_t			average_soc_power;
	uint16_t			average_gfx_power;
	uint16_t			average_core_power[8]; // CPU core power on APUs

	/* Average clocks */
	uint16_t			average_gfxclk_frequency;
	uint16_t			average_socclk_frequency;
	uint16_t			average_uclk_frequency;
	uint16_t			average_fclk_frequency;
	uint16_t			average_vclk_frequency;
	uint16_t			average_dclk_frequency;

	/* Current clocks */
	uint16_t			current_gfxclk;
	uint16_t			current_socclk;
	uint16_t			current_uclk;
	uint16_t			current_fclk;
	uint16_t			current_vclk;
	uint16_t			current_dclk;
	uint16_t			current_coreclk[8]; // CPU core clocks
	uint16_t			current_l3clk[2];

	/* Throttle status (ASIC dependent) */
	uint32_t			throttle_status;

	/* Fans */
	uint16_t			fan_pwm;

	uint16_t			padding[3];

	/* Throttle status (ASIC independent) */
	uint64_t			indep_throttle_status;

	/* Average Temperature */
	uint16_t			average_temperature_gfx; // average gfx temperature on APUs
	uint16_t			average_temperature_soc; // average soc temperature on APUs
	uint16_t			average_temperature_core[8]; // average CPU core temperature on APUs
	uint16_t			average_temperature_l3[2];
};

/* This structure is used to communicate the latest values of the amdgpu metrics.
 * The direction of communication is amdgpu_polling_thread -> amdgpu_get_metrics().
 */
struct amdgpu_common_metrics {
	/* Load level: averaged across the sampling period */
	uint16_t gpu_load_percent;
	// uint16_t mem_load_percent;

	/* Power usage: averaged across the sampling period */
	float average_gfx_power_w;
	float average_cpu_power_w;

	/* Clocks: latest value of the clock */
	uint16_t current_gfxclk_mhz;
	uint16_t current_uclk_mhz;

	/* Temperatures: maximum values over the sampling period */
	uint16_t soc_temp_c;
	uint16_t gpu_temp_c;
	uint16_t apu_cpu_temp_c;

	/* throttling status */
	bool is_power_throttled;
	bool is_current_throttled;
	bool is_temp_throttled;
	bool is_other_throttled;
};

bool amdgpu_verify_metrics(const std::string& path);
void amdgpu_get_metrics();
extern std::string metrics_path;
void amdgpu_get_instant_metrics(struct amdgpu_common_metrics *metrics);
void amdgpu_metrics_polling_thread();
void amdgpu_get_samples_and_copy(struct amdgpu_common_metrics metrics_buffer[METRICS_SAMPLE_COUNT], bool &gpu_load_needs_dividing);
