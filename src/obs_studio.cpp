#include "obs_studio.h"
#include <cstdio>
#if __linux__
#include <sys/mman.h>
#endif

ObsStats* ObsStudio::stats;
void ObsStudio::atexit_func()
{
    if(!stats)
        return;

    stats->running_mangohud = 0;
    if(!stats->running_obs)
    {
#if __linux__
        if(shm_unlink(MANGOHUD_OBS_STATS_SHM) < 0)
            perror("shm_unlink");
#endif
    }
}
