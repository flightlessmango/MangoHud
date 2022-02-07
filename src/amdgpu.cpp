#include "amdgpu.h"
#include "overlay.h"
#include "cpu.h"

std::string metrics_path = "";

void amdgpu_get_metrics()
{
	if (!metrics_path.empty()){
		struct metrics_table_header header;
		std::ifstream in(metrics_path, std::ios_base::in | std::ios_base::binary);
		in.read((char*)&header, sizeof(header));
		if (header.format_revision == 1){
			// Desktop GPUs
			struct gpu_metrics_v1_3 amdgpu_metrics;
			in.clear();
			in.seekg(0);
			in.read((char*)&amdgpu_metrics, sizeof(amdgpu_metrics));
			gpu_info.load = amdgpu_metrics.average_gfx_activity;
			gpu_info.CoreClock = amdgpu_metrics.average_gfxclk_frequency;
			gpu_info.powerUsage = amdgpu_metrics.average_socket_power;
			gpu_info.temp = amdgpu_metrics.temperature_edge;
			gpu_info.MemClock = amdgpu_metrics.current_uclk;
		} else if (header.format_revision == 2){
			// APUs
			struct gpu_metrics_v2_2 amdgpu_metrics;
			in.clear();
			in.seekg(0);
			in.read((char*)&amdgpu_metrics, sizeof(amdgpu_metrics));
			gpu_info.load = amdgpu_metrics.average_gfx_activity;
			gpu_info.CoreClock = amdgpu_metrics.current_gfxclk;
			gpu_info.powerUsage = amdgpu_metrics.average_gfx_power / 1000.f;
			gpu_info.temp = amdgpu_metrics.temperature_gfx / 100;
			gpu_info.MemClock = amdgpu_metrics.current_uclk;
			gpu_info.apu_cpu_power = amdgpu_metrics.average_cpu_power / 1000.f;
			int cpu_temp = 0;
			for (int i = 0; i < cpuStats.GetCPUData().size() / 2; i++)
				if (amdgpu_metrics.temperature_core[i] > cpu_temp)
            		cpu_temp = amdgpu_metrics.temperature_core[i];

			gpu_info.apu_cpu_temp = cpu_temp / 100;
		}
	}
}