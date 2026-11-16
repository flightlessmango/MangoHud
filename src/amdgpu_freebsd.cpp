#include <spdlog/spdlog.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <vector>
#include <xf86drm.h>
#include <libdrm/amdgpu.h>
#include <libdrm/amdgpu_drm.h>
#include "amdgpu.h"
#include "overlay.h"
#include "hud_elements.h"

// There is no sysfs on the BSDs, so everything is queried from the kernel
// through libdrm_amdgpu (the same path Mesa uses).

namespace {

struct amdgpu_handle {
	int fd = -1;
	amdgpu_device_handle dev = nullptr;

	~amdgpu_handle() {
		if (dev)
			amdgpu_device_deinitialize(dev);
		if (fd >= 0)
			close(fd);
	}
};

// Find the render node of a PCI device ("0000:03:00.0") when it's not known
std::string find_render_node(const std::string& pci_dev) {
	unsigned domain, bus, dev, func;
	if (sscanf(pci_dev.c_str(), "%x:%x:%x.%x", &domain, &bus, &dev, &func) != 4)
		return "";

	int n = drmGetDevices2(0, nullptr, 0);
	if (n <= 0)
		return "";

	std::vector<drmDevicePtr> devices(n);
	n = drmGetDevices2(0, devices.data(), n);

	std::string node;
	for (int i = 0; i < n; i++) {
		const drmDevicePtr d = devices[i];
		if (d->bustype != DRM_BUS_PCI || !(d->available_nodes & (1 << DRM_NODE_RENDER)))
			continue;
		if (d->businfo.pci->domain == domain && d->businfo.pci->bus == bus &&
		    d->businfo.pci->dev == dev && d->businfo.pci->func == func) {
			node = d->nodes[DRM_NODE_RENDER];
			break;
		}
	}
	if (n > 0)
		drmFreeDevices(devices.data(), n);
	return node;
}

bool query_sensor(amdgpu_device_handle dev, unsigned type, uint32_t& value) {
	return amdgpu_query_sensor_info(dev, type, sizeof(value), &value) == 0;
}

} // namespace

void AMDGPU::platform_init(const std::string& drm_node) {
	std::string path = drm_node.empty() ? find_render_node(pci_dev) : "/dev/dri/" + drm_node;
	if (path.empty()) {
		SPDLOG_DEBUG("amdgpu: no render node found for {}", pci_dev);
		return;
	}

	auto h = std::make_shared<amdgpu_handle>();
	h->fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
	if (h->fd < 0) {
		SPDLOG_ERROR("amdgpu: failed to open {}", path);
		return;
	}

	uint32_t major, minor;
	if (amdgpu_device_initialize(h->fd, &major, &minor, &h->dev) != 0) {
		SPDLOG_ERROR("amdgpu: amdgpu_device_initialize failed for {}", path);
		return;
	}

	platform = h;
	gpu_metrics_is_valid = true; // sensors are available
}

bool AMDGPU::get_instant_metrics(struct amdgpu_common_metrics *metrics) {
	auto h = std::static_pointer_cast<amdgpu_handle>(platform);
	if (!h)
		return false;

	uint32_t v;
	bool any = false;
	if (query_sensor(h->dev, AMDGPU_INFO_SENSOR_GPU_LOAD, v)) {
		metrics->gpu_load_percent = v;
		any = true;
	}
	if (query_sensor(h->dev, AMDGPU_INFO_SENSOR_GPU_AVG_POWER, v)) { // watts
		metrics->average_gfx_power_w = v;
		any = true;
	}
	if (query_sensor(h->dev, AMDGPU_INFO_SENSOR_GFX_SCLK, v)) {
		metrics->current_gfxclk_mhz = v;
		any = true;
	}
	if (query_sensor(h->dev, AMDGPU_INFO_SENSOR_GFX_MCLK, v)) {
		metrics->current_uclk_mhz = v;
		any = true;
	}
	if (query_sensor(h->dev, AMDGPU_INFO_SENSOR_GPU_TEMP, v)) {
		metrics->gpu_temp_c = v / 1000; // millidegrees Celsius
		any = true;
	}
	return any;
}

void AMDGPU::get_sysfs_metrics() {
	auto h = std::static_pointer_cast<amdgpu_handle>(platform);
	if (!h)
		return;

	constexpr float GiB = 1024.f * 1024.f * 1024.f;

	struct amdgpu_heap_info heap {};
	if (amdgpu_query_heap_info(h->dev, AMDGPU_GEM_DOMAIN_VRAM, 0, &heap) == 0) {
		metrics.memoryTotal = heap.heap_size / GiB;
		metrics.sys_vram_used = heap.heap_usage / GiB;
	}

	if (amdgpu_query_heap_info(h->dev, AMDGPU_GEM_DOMAIN_GTT, 0, &heap) == 0)
		metrics.gtt_used = heap.heap_usage / GiB;

	uint32_t v;
	if (query_sensor(h->dev, AMDGPU_INFO_SENSOR_GFX_SCLK, v))
		metrics.CoreClock = v;
	if (query_sensor(h->dev, AMDGPU_INFO_SENSOR_VDDGFX, v))
		metrics.voltage = v; // mV
}
