#pragma once
#include <stdint.h>

#define MANGOHUD_OBS_NAME "mangohud_obs"
#define MANGOHUD_OBS_STATS_SHM "/mangohud_ObsStats"

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

