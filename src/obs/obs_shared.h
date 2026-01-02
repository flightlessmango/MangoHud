#pragma once
#define MANGOHUD_OBS_NAME "mangohud_obs"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int mangohud_obs_get_shmpath(char* str, size_t n);

int mangohud_obs_get_lastrecording(char* str, size_t n);
#ifdef __cplusplus
}
#endif


