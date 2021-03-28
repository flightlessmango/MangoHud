#pragma once
#ifndef MANGOHUD_GPU_H
#define MANGOHUD_GPU_H

#include <stdio.h>
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
    int powerUsage;
};

extern struct gpuInfo gpu_info;

void getNvidiaGpuInfo(void);
void getAmdGpuInfo(void);
bool checkNvidia(const char *pci_dev);
extern void nvapi_util();
extern bool checkNVAPI();

// Amd windows
int query_adl(void);
uint32_t adl_vendorid(void);
int initializeADL(void);
extern bool init_adl;

#endif //MANGOHUD_GPU_H
