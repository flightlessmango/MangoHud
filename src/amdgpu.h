#pragma once
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <string>
#include "overlay_params.h"
#include <mutex>
#include <condition_variable>
#include <vector>
#include <sys/param.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include "gpu_metrics_util.h"

#ifndef TEST_ONLY
#include "gpu_fdinfo.h"
#endif

#define NUM_HBM_INSTANCES 4
#define TEMP_HOTSPOT_BIT 36ull
#define UPDATE_METRIC_AVERAGE(FIELD) do { int value_sum = 0; for (size_t s=0; s < METRICS_SAMPLE_COUNT; s++) { value_sum += metrics_buffer[s].FIELD; } amdgpu_common_metrics.FIELD = value_sum / METRICS_SAMPLE_COUNT; } while(0)
#define UPDATE_METRIC_AVERAGE_FLOAT(FIELD) do { float value_sum = 0; for (size_t s=0; s < METRICS_SAMPLE_COUNT; s++) { value_sum += metrics_buffer[s].FIELD; } amdgpu_common_metrics.FIELD = value_sum / METRICS_SAMPLE_COUNT; } while(0)
#define UPDATE_METRIC_MAX(FIELD) do { int cur_max = metrics_buffer[0].FIELD; for (size_t s=1; s < METRICS_SAMPLE_COUNT; s++) { cur_max = MAX(cur_max, metrics_buffer[s].FIELD); }; amdgpu_common_metrics.FIELD = cur_max; } while(0)
#define UPDATE_METRIC_LAST(FIELD) do { amdgpu_common_metrics.FIELD = metrics_buffer[METRICS_SAMPLE_COUNT - 1].FIELD; } while(0)
#ifdef _WIN32
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif

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


struct gpu_metrics_v2_4 {
	struct metrics_table_header	common_header;

	/* Temperature (unit: centi-Celsius) */
	uint16_t			temperature_gfx;
	uint16_t			temperature_soc;
	uint16_t			temperature_core[8];
	uint16_t			temperature_l3[2];

	/* Utilization (unit: centi) */
	uint16_t			average_gfx_activity;
	uint16_t			average_mm_activity;

	/* Driver attached timestamp (in ns) */
	uint64_t			system_clock_counter;

	/* Power/Energy (unit: mW) */
	uint16_t			average_socket_power;
	uint16_t			average_cpu_power;
	uint16_t			average_soc_power;
	uint16_t			average_gfx_power;
	uint16_t			average_core_power[8];

	/* Average clocks (unit: MHz) */
	uint16_t			average_gfxclk_frequency;
	uint16_t			average_socclk_frequency;
	uint16_t			average_uclk_frequency;
	uint16_t			average_fclk_frequency;
	uint16_t			average_vclk_frequency;
	uint16_t			average_dclk_frequency;

	/* Current clocks (unit: MHz) */
	uint16_t			current_gfxclk;
	uint16_t			current_socclk;
	uint16_t			current_uclk;
	uint16_t			current_fclk;
	uint16_t			current_vclk;
	uint16_t			current_dclk;
	uint16_t			current_coreclk[8];
	uint16_t			current_l3clk[2];

	/* Throttle status (ASIC dependent) */
	uint32_t			throttle_status;

	/* Fans */
	uint16_t			fan_pwm;

	uint16_t			padding[3];

	/* Throttle status (ASIC independent) */
	uint64_t			indep_throttle_status;

	/* Average Temperature (unit: centi-Celsius) */
	uint16_t			average_temperature_gfx;
	uint16_t			average_temperature_soc;
	uint16_t			average_temperature_core[8];
	uint16_t			average_temperature_l3[2];

	/* Power/Voltage (unit: mV) */
	uint16_t			average_cpu_voltage;
	uint16_t			average_soc_voltage;
	uint16_t			average_gfx_voltage;

	/* Power/Current (unit: mA) */
	uint16_t			average_cpu_current;
	uint16_t			average_soc_current;
	uint16_t			average_gfx_current;
};

struct gpu_metrics_v3_0 {
	struct metrics_table_header	common_header;

	/* Temperature */
	/* gfx temperature on APUs */
	uint16_t			temperature_gfx;
	/* soc temperature on APUs */
	uint16_t			temperature_soc;
	/* CPU core temperature on APUs */
	uint16_t			temperature_core[16];
	/* skin temperature on APUs */
	uint16_t			temperature_skin;

	/* Utilization */
	/* time filtered GFX busy % [0-100] */
	uint16_t			average_gfx_activity;
	/* time filtered VCN busy % [0-100] */
	uint16_t			average_vcn_activity;
	/* time filtered IPU per-column busy % [0-100] */
	uint16_t			average_ipu_activity[8];
	/* time filtered per-core C0 residency % [0-100]*/
	uint16_t			average_core_c0_activity[16];
	/* time filtered DRAM read bandwidth [MB/sec] */
	uint16_t			average_dram_reads;
	/* time filtered DRAM write bandwidth [MB/sec] */
	uint16_t			average_dram_writes;
	/* time filtered IPU read bandwidth [MB/sec] */
	uint16_t			average_ipu_reads;
	/* time filtered IPU write bandwidth [MB/sec] */
	uint16_t			average_ipu_writes;

	/* Driver attached timestamp (in ns) */
	uint64_t			system_clock_counter;

	/* Power/Energy */
	/* time filtered power used for PPT/STAPM [APU+dGPU] [mW] */
	uint32_t			average_socket_power;
	/* time filtered IPU power [mW] */
	uint16_t			average_ipu_power;
	/* time filtered APU power [mW] */
	uint32_t			average_apu_power;
	/* time filtered GFX power [mW] */
	uint32_t			average_gfx_power;
	/* time filtered dGPU power [mW] */
	uint32_t			average_dgpu_power;
	/* time filtered sum of core power across all cores in the socket [mW] */
	uint32_t			average_all_core_power;
	/* calculated core power [mW] */
	uint16_t			average_core_power[16];
	/* time filtered total system power [mW] */
	uint16_t			average_sys_power;
	/* maximum IRM defined STAPM power limit [mW] */
	uint16_t			stapm_power_limit;
	/* time filtered STAPM power limit [mW] */
	uint16_t			current_stapm_power_limit;

	/* time filtered clocks [MHz] */
	uint16_t			average_gfxclk_frequency;
	uint16_t			average_socclk_frequency;
	uint16_t			average_vpeclk_frequency;
	uint16_t			average_ipuclk_frequency;
	uint16_t			average_fclk_frequency;
	uint16_t			average_vclk_frequency;
	uint16_t			average_uclk_frequency;
	uint16_t			average_mpipu_frequency;

	/* Current clocks */
	/* target core frequency [MHz] */
	uint16_t			current_coreclk[16];
	/* CCLK frequency limit enforced on classic cores [MHz] */
	uint16_t			current_core_maxfreq;
	/* GFXCLK frequency limit enforced on GFX [MHz] */
	uint16_t			current_gfx_maxfreq;

	/* Throttle Residency (ASIC dependent) */
	uint32_t			throttle_residency_prochot;
	uint32_t			throttle_residency_spl;
	uint32_t			throttle_residency_fppt;
	uint32_t			throttle_residency_sppt;
	uint32_t			throttle_residency_thm_core;
	uint32_t			throttle_residency_thm_gfx;
	uint32_t			throttle_residency_thm_soc;

	/* Metrics table alpha filter time constant [us] */
	uint32_t			time_filter_alphavalue;
};

struct amdgpu_files
{
    FILE *vram_total;
    FILE *vram_used;
    /* The following can be NULL, in that case we're using the gpu_metrics node */
    FILE *busy;
    FILE *temp;
    FILE *junction_temp;
    FILE *memory_temp;
    FILE *core_clock;
    FILE *memory_clock;
    FILE *power_usage;
    FILE *power_limit;
    FILE *gtt_used;
    FILE *fan;
    FILE *gpu_voltage_soc;
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

	uint16_t fan_speed;
};

extern std::string metrics_path;

class AMDGPU {
	public:
		bool is_apu = false;
		std::shared_ptr<Throttling> throttling;

    	AMDGPU(std::string pci_dev, uint32_t device_id, uint32_t vendor_id);

		~AMDGPU() {
			stop_thread = true;
			if (thread.joinable())
				thread.join();
		}

		bool verify_metrics(const std::string& path);
		void get_instant_metrics(struct amdgpu_common_metrics *metrics);
		void get_samples_and_copy(struct amdgpu_common_metrics metrics_buffer[METRICS_SAMPLE_COUNT],
								  bool &gpu_load_needs_dividing);

        gpu_metrics copy_metrics() {
            std::lock_guard<std::mutex> lock(metrics_mutex);
            return metrics;
        };

        void pause() {
            paused = true;
            cond_var.notify_one();
        };

        void resume() {
            paused = false;
            cond_var.notify_one();
        }

	private:
		std::string pci_dev;
		std::string gpu_metrics_path;
		uint32_t device_id;
		uint32_t vendor_id;
		std::condition_variable amdgpu_c;
		std::thread thread;
		struct amdgpu_files sysfs_nodes = {};
		bool gpu_metrics_is_valid = false;
		std::condition_variable cond_var;
		std::atomic<bool> stop_thread{false};
        std::atomic<bool> paused{false};
		std::mutex metrics_mutex;
		gpu_metrics metrics;
		struct amdgpu_common_metrics amdgpu_common_metrics;

#ifndef TEST_ONLY
		std::unique_ptr<GPU_fdinfo> fdinfo_helper;
#endif

		void get_sysfs_metrics();
		void metrics_polling_thread();
};
