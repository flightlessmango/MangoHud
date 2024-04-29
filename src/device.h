#pragma once
#ifndef MANGOHUD_DEVICE_H
#define MANGOHUD_DEVICE_H
#include <vector>
#include <string>
#include "overlay_params.h"
struct overlay_params;
struct device_batt {
    std::string battery;
    std::string name;
    bool report_percent;
    std::string battery_percent;
    bool is_charging;
};

extern std::vector<device_batt> device_data;
extern std::mutex device_lock;

extern bool device_found;
extern int device_count;
void device_update(const overlay_params& params);
void device_info();


#endif // MANGOHUD_DEVICE_H
