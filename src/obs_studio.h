#pragma once
#include <stdint.h>
struct obs_studio_data
{
    uint32_t time;
    uint64_t bytes;
};
#define MANGOHUD_OBS_NAME "mangohud_obs"
#define MANGOHUD_OBS_STATS_SHM "/mangohud_ObsStats"
#define MANGOHUD_OBS_STATS_SEM "/mangohud_ObsStatsSem"

