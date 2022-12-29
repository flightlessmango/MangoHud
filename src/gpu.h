#pragma once
#ifndef MANGOHUD_GPU_H
#define MANGOHUD_GPU_H

#include <cstdio>
#include <cstdint>
#include "overlay_params.h"

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
    FILE *gtt_used;
    FILE *fan;
};

extern amdgpu_files amdgpu;

struct gpuInfo{
    int load;
    int temp;
    int junction_temp {-1};
    int memory_temp {-1};
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
    float gtt_used;
    int fan_speed;
};

extern struct gpuInfo gpu_info;

void getNvidiaGpuInfo(const struct overlay_params& params);
void getAmdGpuInfo(void);
void getIntelGpuInfo(const char *drm_dev);
bool checkNvidia(const char *pci_dev);
extern void nvapi_util();
extern bool checkNVAPI();
#endif //MANGOHUD_GPU_H
