#pragma once
#ifndef MANGOHUD_GPU_H
#define MANGOHUD_GPU_H

#include <stdio.h>
#include <unordered_map>
#include <cstdint>
#include <string>

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

extern std::unordered_map<int32_t, amdgpu_files> amdgpu;

struct gpuInfo{
    std::string deviceName;
    int load;
    int temp;
    float memoryUsed;
    float memoryTotal;
    int MemClock;
    int CoreClock;
    int powerUsage;
};

extern std::unordered_map<int32_t, struct gpuInfo> gpu_info;

void getNvidiaGpuInfo(int32_t deviceID);
void getAmdGpuInfo(int32_t deviceID);
bool checkNvidia(const char *pci_dev);
extern void nvapi_util();
extern bool checkNVAPI();
#endif //MANGOHUD_GPU_H
