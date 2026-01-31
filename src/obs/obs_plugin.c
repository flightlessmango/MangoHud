#include "obs_plugin.h"
#include "obs_shared.h"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <stdio.h>
#include <string.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static struct
{
    struct obs_output* output;
    float time_hires;
    ObsStats* sdata;
}data;

void mangohud_obs_frontend_event_callback(enum obs_frontend_event event, void* private_data);
void mangohud_obs_frontend_tick_callback(void* arg, float sec);
void mangohud_obs_prefix_exe(void);

void mangohud_obs_frontend_tick_callback(void* arg, float sec)
{
    if(data.sdata->recording)
    {
        data.time_hires += sec;
        data.sdata->time = data.time_hires;
        data.sdata->bytes = obs_output_get_total_bytes(data.output);
    }
}
void mangohud_obs_prefix_exe(void)
{
    /*
     * TODO: we are currently renaming the output file once recording is finished.
     * ideally obs should allow us to change output filename but there doesn't
     * seem to exist such an API call
     *
     * changing filename will get complicated with multiple instances of mangohud
     * the last instance will prevail
     */
    char recording[1024];
    if(!mangohud_obs_get_lastrecording(recording, sizeof(recording)))
        return;
    const char* filename = strrchr(recording, '/') + 1;

    char* basename = strdup(recording);
    *strrchr(basename, '/') = 0;

    const size_t newpath_sz = PATH_MAX;
    char* newpath = calloc(1, newpath_sz);
    snprintf(newpath, newpath_sz, "%s/%s_%s", basename, data.sdata->exe, filename);
    rename(recording, newpath);
    free(newpath);
    free(basename);
}
void mangohud_obs_frontend_event_callback(enum obs_frontend_event event, void* private_data)
{
    switch (event) {

        case OBS_FRONTEND_EVENT_RECORDING_STARTING:
            {
                data.output = obs_frontend_get_recording_output();

                blog(LOG_INFO, "%s: starting recording", MANGOHUD_OBS_NAME);
                data.time_hires = 0;
                data.sdata->bytes = 0;
                data.sdata->recording = 1;
                break;
            }

        case OBS_FRONTEND_EVENT_RECORDING_STOPPING:
            {
                blog(LOG_INFO, "%s: stopping recording", MANGOHUD_OBS_NAME);
                data.sdata->recording = 0;
                obs_output_release(data.output);
                data.output = NULL;
                break;
            }

        case OBS_FRONTEND_EVENT_EXIT:
            {
                blog(LOG_INFO, "%s: exiting", MANGOHUD_OBS_NAME);
                if(data.output)
                    obs_output_release(data.output);
                if(data.sdata->prefix_exe && data.sdata->running_mangohud && data.sdata->recording)
                    mangohud_obs_prefix_exe();
                data.sdata->recording = 0;
                data.sdata->running_obs = 0;
                break;
            }

        case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
            {

                if(!data.sdata->prefix_exe || !data.sdata->running_mangohud)
                    break;
                mangohud_obs_prefix_exe();

                break;
            }

        default:
            break;
    }
}

bool obs_module_load(void)
{
#ifdef __linux__
    int fd;
    char shmpath[1024];
    mangohud_obs_get_shmpath(shmpath, sizeof(shmpath));
    if((fd = shm_open(shmpath, O_CREAT | O_RDWR, 0666)) < 0)
    {
        blog(LOG_ERROR, "%s: shm_open error %s", MANGOHUD_OBS_NAME, strerror(errno));
        return false;
    }
    if(fd > 0 && ftruncate(fd, sizeof(ObsStats)) < 0)
    {
        blog(LOG_ERROR, "%s: ftruncate error %s", MANGOHUD_OBS_NAME, strerror(errno));
        return false;
    }
    if((data.sdata = mmap(NULL, sizeof(ObsStats), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED)
    {
        blog(LOG_ERROR, "%s: mmap error %s", MANGOHUD_OBS_NAME, strerror(errno));
        return false;
    }
    close(fd);
    data.sdata->running_obs = 1;
#endif
    obs_frontend_add_event_callback(mangohud_obs_frontend_event_callback, NULL);
    obs_add_tick_callback(mangohud_obs_frontend_tick_callback, NULL);

    blog(LOG_INFO, "%s: plugin loaded successfully", MANGOHUD_OBS_NAME);

    return true;
}

void obs_module_unload()
{
    if(data.sdata)
    {
        char shmpath[1024];
        mangohud_obs_get_shmpath(shmpath, sizeof(shmpath));
        data.sdata->running_obs = 0;
        if(!data.sdata->running_mangohud){
            if(shm_unlink(shmpath) < 0)
                blog(LOG_ERROR, "%s: shm_unlink error %s", MANGOHUD_OBS_NAME, strerror(errno));
        }

    }
    obs_frontend_remove_event_callback(mangohud_obs_frontend_event_callback, NULL);
    obs_remove_tick_callback(mangohud_obs_frontend_tick_callback, NULL);

    blog(LOG_INFO, "%s: plugin unloaded", MANGOHUD_OBS_NAME);
}

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Dennis Maina <dennismyner7@gmail.com>")
OBS_MODULE_USE_DEFAULT_LOCALE(OBS_STUDIO_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
    return MANGOHUD_OBS_NAME;
}

MODULE_EXPORT const char *obs_module_description(void)
{
    return obs_module_text("Mangohud plugin for OBS");
}
