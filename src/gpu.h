#pragma once
#ifndef MANGOHUD_GPU_H
#define MANGOHUD_GPU_H

#include <stdio.h>

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
    int powerUsage;
};

extern struct gpuInfo gpu_info;

void getNvidiaGpuInfo(void);
void getAmdGpuInfo(void);
#ifdef HAVE_LIBDRM_AMDGPU
void getAmdGpuInfo_libdrm();
bool amdgpu_open(const char *path);
#endif
extern decltype(&getAmdGpuInfo) getAmdGpuInfo_actual;
bool checkNvidia(const char *pci_dev);
extern void nvapi_util();
extern bool checkNVAPI();
#endif //MANGOHUD_GPU_H
