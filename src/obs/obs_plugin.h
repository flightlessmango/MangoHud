#pragma once
#include <stdint.h>

typedef struct
{
    int recording;
    int prefix_exe;
    int running_mangohud;
    int running_obs;
    uint32_t time;
    uint64_t bytes;
    char exe[64];
}ObsStats;

