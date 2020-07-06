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
};

extern struct nvctrlInfo nvctrl_info;
extern bool nvctrlSuccess;
bool checkXNVCtrl(void);
void getNvctrlInfo(void);
char *get_attr_target_string(int attr, int target_type, int target_id);

#endif //MANGOHUD_NVCTRL_H
