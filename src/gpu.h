#pragma once
#ifndef MANGOHUD_GPU_H
#define MANGOHUD_GPU_H

#include <cstdio>
#include <cstdint>

struct amdgpu_files
{
    FILE *busy;
    FILE *temp;
    FILE *vram_total;
    FILE *vram_used;
    FILE *core_clock;
    FILE *memory_clock;
    FILE *power_usage;
};

extern amdgpu_files amdgpu;

struct gpuInfo{
    int load;
    int temp;
    float memoryUsed;
    float memoryTotal;
    int MemClock;
    int CoreClock;
    float powerUsage;
    float apu_cpu_power;
    int apu_cpu_temp;
};

extern struct gpuInfo gpu_info;

void getNvidiaGpuInfo(void);
void getAmdGpuInfo(void);
#ifdef HAVE_LIBDRM_AMDGPU
void getAmdGpuInfo_libdrm();
bool amdgpu_open(const char *path);
void amdgpu_set_sampling_period(uint32_t period);
#endif
extern decltype(&getAmdGpuInfo) getAmdGpuInfo_actual;
bool checkNvidia(const char *pci_dev);
extern void nvapi_util();
extern bool checkNVAPI();
#endif //MANGOHUD_GPU_H
