#include <stdio.h>
#include <regex>
#include "nvctrl.h"
#include "loaders/loader_nvctrl.h"
#include <iostream>

Display *display = XOpenDisplay(NULL);
libnvctrl_loader nvctrl("libXNVCtrl.so");

struct nvctrlInfo nvctrl_info;

char* get_attr_target_string(int attr, int target_type, int target_id) {
        char* c;
        
        if (!nvctrl.XNVCTRLQueryTargetStringAttribute(display, target_type, target_id, 0, attr, &c)) {
                fprintf(stderr, "Failed to query attribute.");
                
        }
        return c;
}

void getNvctrlInfo(){
    char* utilization = get_attr_target_string(NV_CTRL_STRING_GPU_UTILIZATION, NV_CTRL_TARGET_TYPE_GPU, 0);
    char* s = utilization;
    strtok(s, ",");
    while (s != NULL)
    {
        std::string str = s;
        if (str.find("graphics") != std::string::npos){
            str = std::regex_replace(str, std::regex(R"([^0-9.])"), "");
            nvctrl_info.load = std::stoi(str);
        }
        s = strtok (NULL, ",");
    }

    char* freq = get_attr_target_string(NV_CTRL_STRING_GPU_CURRENT_CLOCK_FREQS, NV_CTRL_TARGET_TYPE_GPU, 0);
    char* freq_str = freq;
    strtok(freq_str, ",");
    while (freq_str != NULL)
    {
        std::string str = freq_str;
        if (str.find("nvclock=") != std::string::npos){
            str = std::regex_replace(str, std::regex(R"([^0-9.])"), "");
            nvctrl_info.CoreClock = std::stoi(str);
        }
        if (str.find("memclock=") != std::string::npos){
            str = std::regex_replace(str, std::regex(R"([^0-9.])"), "");
            nvctrl_info.MemClock = std::stoi(str);
        }
        freq_str = strtok (NULL, ",");
    }
    printf("coreclock: %i\n", nvctrl_info.CoreClock);
    printf("memclock: %i\n", nvctrl_info.MemClock);

    int64_t temp;
    nvctrl.XNVCTRLQueryTargetAttribute64(display,
                        NV_CTRL_TARGET_TYPE_GPU,
                        0,
                        0,
                        NV_CTRL_GPU_CORE_TEMPERATURE,
                        &temp);
    nvctrl_info.temp = temp;

    int64_t memtotal;
            nvctrl.XNVCTRLQueryTargetAttribute64(display,
                        NV_CTRL_TARGET_TYPE_GPU,
                        0,
                        0,
                        NV_CTRL_TOTAL_DEDICATED_GPU_MEMORY,
                        &memtotal);
    nvctrl_info.memoryTotal = memtotal;

    int64_t memused;
            nvctrl.XNVCTRLQueryTargetAttribute64(display,
                        NV_CTRL_TARGET_TYPE_GPU,
                        0,
                        0,
                        NV_CTRL_USED_DEDICATED_GPU_MEMORY,
                        &memused);
    nvctrl_info.memoryUsed = memused;    
        
    free(utilization);
    free(freq);
}