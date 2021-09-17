#pragma once
#ifndef MANGOHUD_NVIDIA_INFO_H
#define MANGOHUD_NVIDIA_INFO_H

#include <nvml.h>
#include "overlay_params.h"

extern bool nvmlSuccess;

bool checkNVML(const char* pciBusId, nvmlDevice_t& device, uint32_t& device_id);
bool getNVMLInfo(nvmlDevice_t device, const struct overlay_params& params);

#endif //MANGOHUD_NVIDIA_INFO_H
