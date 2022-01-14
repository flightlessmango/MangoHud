#pragma once
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <vector>

struct metrics_table_header {
        uint16_t                        structure_size;
        uint8_t                         format_revision;
        uint8_t                         content_revision;
};

struct gpu_metrics_v2_2 {
        struct metrics_table_header     common_header;

        /* Temperature */
        uint16_t                        temperature_gfx; // gfx temperature on APUs
        uint16_t                        temperature_soc; // soc temperature on APUs
        uint16_t                        temperature_core[8]; // CPU core temperature on APUs
        uint16_t                        temperature_l3[2];

        /* Utilization */
        uint16_t                        average_gfx_activity;
        uint16_t                        average_mm_activity; // UVD or VCN

        /* Driver attached timestamp (in ns) */
        uint64_t                        system_clock_counter;

        /* Power/Energy */
        uint16_t                        average_socket_power; // dGPU + APU power on A + A platform
        uint16_t                        average_cpu_power;
        uint16_t                        average_soc_power;
        uint16_t                        average_gfx_power;
        uint16_t                        average_core_power[8]; // CPU core power on APUs

        /* Average clocks */
        uint16_t                        average_gfxclk_frequency;
        uint16_t                        average_socclk_frequency;
        uint16_t                        average_uclk_frequency;
        uint16_t                        average_fclk_frequency;
        uint16_t                        average_vclk_frequency;
        uint16_t                        average_dclk_frequency;

        /* Current clocks */
        uint16_t                        current_gfxclk;
        uint16_t                        current_socclk;
        uint16_t                        current_uclk;
        uint16_t                        current_fclk;
        uint16_t                        current_vclk;
        uint16_t                        current_dclk;
        uint16_t                        current_coreclk[8]; // CPU core clocks
        uint16_t                        current_l3clk[2];

        /* Throttle status (ASIC dependent) */
        uint32_t                        throttle_status;

        /* Fans */
        uint16_t                        fan_pwm;

        uint16_t                        padding[3];

        /* Throttle status (ASIC independent) */
        uint64_t                        indep_throttle_status;
};

extern void amdgpu_get_metrics();