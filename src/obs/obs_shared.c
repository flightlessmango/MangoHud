#include "obs_shared.h"
#include <stdio.h>
#define MANGOHUD_OBS_STATS_SHM "/mangohud_ObsStats"

#ifdef __linux__
#include <unistd.h>
#include <dlfcn.h>
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

int mangohud_obs_get_lastrecording(char* str, size_t n){
    int ret = 0;
#ifdef __linux__
    void* dl = NULL;
    const char* dlname = "libobs-frontend-api.so";

    const char* _obs_frontend_get_last_recording_sym_name = "obs_frontend_get_last_recording";
    char* (*_obs_frontend_get_last_recording_sym)();

    const char* _bfree_name = "bfree";
    void (*_bfree_sym)(void*);

    char* lastrecording = NULL;

    if((dl = dlopen(dlname, RTLD_LAZY)) == NULL)
    {
        fprintf(stderr, "[%s]: cannot load %s: %s\n", MANGOHUD_OBS_NAME, dlname, dlerror());
        goto cleanup;
    }
    if((_obs_frontend_get_last_recording_sym = dlsym(dl, _obs_frontend_get_last_recording_sym_name)) == NULL )
    {
        fprintf(stderr, "[%s]: cannot dlsym %s. we need at least libobs v29.0.0\n", MANGOHUD_OBS_NAME, _obs_frontend_get_last_recording_sym_name);
        goto cleanup;
    }
    if((_bfree_sym = dlsym(dl, _bfree_name)) == NULL)
    {
        fprintf(stderr, "[%s]: cannot locate bfree. crtitical obs error please reinstall obs\n", MANGOHUD_OBS_NAME);
        goto cleanup;
    }

    if((lastrecording = _obs_frontend_get_last_recording_sym()) == NULL)
    {
        fprintf(stderr, "[%s]: cannot get last recording. critical obs error please reinstall obs\n", MANGOHUD_OBS_NAME);
        goto cleanup;
    }

    snprintf(str, n, "%s", lastrecording);
    _bfree_sym(lastrecording);
    ret = 1;
cleanup:
    if(dl)
        dlclose(dl);
#endif
    return ret;
}
