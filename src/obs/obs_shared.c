#include "obs_shared.h"
#include <stdio.h>
#define MANGOHUD_OBS_STATS_SHM "/mangohud_ObsStats"

#ifdef __linux__
#include <unistd.h>
#endif
int mangohud_obs_get_shmpath(char* str, size_t n)
{
#ifdef __linux__
    /* stored in /dev/shm, we need a unique instance for every user */
    snprintf(str, n, "%s_%u", MANGOHUD_OBS_STATS_SHM, getuid());
    return 1;
#endif
    return 0;
}
