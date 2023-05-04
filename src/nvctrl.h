#pragma once
#ifndef MANGOHUD_NVCTRL_H
#define MANGOHUD_NVCTRL_H

struct nvctrlInfo{
    int load;
    int temp;
    float memoryUsed;
    float memoryTotal;
    int MemClock;
    int CoreClock;
    int fan_speed;
};

extern struct nvctrlInfo nvctrl_info;
extern bool nvctrlSuccess;
bool checkXNVCtrl(void);
void getNvctrlInfo(void);
int64_t getNvctrlFanSpeed();

#endif //MANGOHUD_NVCTRL_H
