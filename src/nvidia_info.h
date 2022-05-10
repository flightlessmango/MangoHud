#pragma once
#ifndef MANGOHUD_NVIDIA_INFO_H
#define MANGOHUD_NVIDIA_INFO_H

#include <nvml.h>
#include "overlay_params.h"

extern nvmlReturn_t result;
extern unsigned int nvidiaTemp, processSamplesCount, *vgpuInstanceSamplesCount, nvidiaCoreClock, nvidiaMemClock, nvidiaPowerUsage;
extern nvmlDevice_t nvidiaDevice;
extern struct nvmlUtilization_st nvidiaUtilization;
extern struct nvmlMemory_st nvidiaMemory;
extern bool nvmlSuccess;
extern unsigned long long nvml_throttle_reasons;

bool checkNVML(const char* pciBusId);
bool getNVMLInfo(const struct overlay_params& params);

#endif //MANGOHUD_NVIDIA_INFO_H
