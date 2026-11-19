#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/pciio.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <algorithm>
#include "gpu.h"

// Devices are found on the PCI bus rather than through /dev/dri: the
// proprietary NVIDIA driver doesn't always provide a DRM render node.

#ifndef PCIC_DISPLAY
#define PCIC_DISPLAY 0x03
#endif

static std::string driver_for_vendor(const struct pci_conf& pc) {
    switch (pc.pc_vendor) {
    case 0x1002: return "amdgpu";
    case 0x10de: return "nvidia";
    default:     return pc.pd_name; // reported as unsupported by the caller
    }
}

std::vector<gpu_device> gpu_enumerate() {
    std::vector<gpu_device> devices;

    int fd = open("/dev/pci", O_RDONLY);
    if (fd < 0) {
        SPDLOG_ERROR("Failed to open /dev/pci");
        return devices;
    }

    struct pci_conf matches[32];
    struct pci_conf_io pc {};
    pc.match_buf_len = sizeof(matches);
    pc.matches = matches;

    do {
        if (ioctl(fd, PCIOCGETCONF, &pc) == -1) {
            SPDLOG_ERROR("PCIOCGETCONF failed");
            break;
        }
        if (pc.status == PCI_GETCONF_ERROR) {
            SPDLOG_ERROR("PCIOCGETCONF reported an error");
            break;
        }

        for (unsigned i = 0; i < pc.num_matches; i++) {
            const struct pci_conf& c = matches[i];
            if (c.pc_class != PCIC_DISPLAY)
                continue;

            gpu_device dev;
            char addr[16];
            snprintf(addr, sizeof(addr), "%04x:%02x:%02x.%x",
                     (unsigned)c.pc_sel.pc_domain, (unsigned)c.pc_sel.pc_bus,
                     (unsigned)c.pc_sel.pc_dev, (unsigned)c.pc_sel.pc_func);
            dev.pci_dev = addr;
            dev.vendor_id = c.pc_vendor;
            dev.device_id = c.pc_device;
            dev.driver = driver_for_vendor(c);
            devices.push_back(std::move(dev));
        }
    } while (pc.status == PCI_GETCONF_MORE_DEVS);

    close(fd);

    std::sort(devices.begin(), devices.end(),
              [](const gpu_device& a, const gpu_device& b) { return a.pci_dev < b.pci_dev; });
    return devices;
}
