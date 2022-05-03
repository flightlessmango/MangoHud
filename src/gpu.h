#pragma once
#ifndef MANGOHUD_GPU_H
#define MANGOHUD_GPU_H

#include <cstdio>
#include <cstdint>

struct amdgpu_files
{
    FILE *vram_total;
    FILE *vram_used;
    /* The following can be NULL, in that case we're using the gpu_metrics node */
    FILE *busy;
    FILE *temp;
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
    bool is_power_throttled;
    bool is_current_throttled;
    bool is_temp_throttled;
    bool is_other_throttled;
};

extern struct gpuInfo gpu_info;

void getNvidiaGpuInfo(void);
void getAmdGpuInfo(void);
extern decltype(&getAmdGpuInfo) getAmdGpuInfo_actual;
bool checkNvidia(const char *pci_dev);
extern void nvapi_util();
extern bool checkNVAPI();
#endif //MANGOHUD_GPU_H
