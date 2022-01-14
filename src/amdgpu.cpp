#include "amdgpu.h"
#include "overlay.h"

struct gpu_metrics_v2_2 amdgpu_metrics;

void amdgpu_get_metrics()
{
	std::ifstream in("/sys/class/drm/renderD128/device/gpu_metrics", std::ios_base::in | std::ios_base::binary);
	in.read((char*)&amdgpu_metrics, sizeof(amdgpu_metrics));
}