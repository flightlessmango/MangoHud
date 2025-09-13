#include "obs_studio.h"
#include "obs.h"
#include <obs-module.h>
#include <obs-nix-platform.h>
#include <obs-frontend-api.h>

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <errno.h>

static struct
{
    struct obs_output* output;
    bool recording;
    float time_hires;
    struct obs_studio_data* sdata;
    sem_t* capture_stats_sem;
}data;

void mangohud_obs_frontend_event_callback(enum obs_frontend_event event, void* private_data);
void mangohud_obs_frontend_tick_callback(void* arg, float sec);

void mangohud_obs_frontend_tick_callback(void* arg, float sec)
{
    if(data.recording)
    {
        data.time_hires += sec;
        data.sdata->time = data.time_hires;
        data.sdata->bytes = obs_output_get_total_bytes(data.output);
    }
}
void mangohud_obs_frontend_event_callback(enum obs_frontend_event event, void* private_data)
{
    switch (event) {

        case OBS_FRONTEND_EVENT_RECORDING_STARTING:
            {
                /* afaict refs seem to persist */
                if(!data.output)
                    data.output = obs_frontend_get_recording_output();
                data.recording = true;
                data.time_hires = 0;
                data.sdata = NULL;

                int fd_shm;
                if((fd_shm = shm_open(MANGOHUD_OBS_STATS_SHM, O_CREAT | O_RDWR, 0666)) < 0)
                {
                    blog(LOG_ERROR, "shm_open error %s", strerror(errno));
                }
                if(fd_shm > 0 && ftruncate(fd_shm, sizeof(struct obs_studio_data)) < 0)
                {
                    blog(LOG_ERROR, "ftruncate error %s", strerror(errno));
                }

                if((data.sdata = mmap(NULL, sizeof(struct obs_studio_data), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0)))
                {
                    memset(data.sdata, 0, sizeof(struct obs_studio_data));
                    close(fd_shm);
                }

                if((data.capture_stats_sem = sem_open(MANGOHUD_OBS_STATS_SEM, O_CREAT, 0644, 0)) == SEM_FAILED)
                {
                    blog(LOG_ERROR, "sem_open error %s", strerror(errno));
                }

                if(sem_post(data.capture_stats_sem) < 0)
                {
                    blog(LOG_ERROR, "sem_post error %s", strerror(errno));
                }
                break;
            }

        case OBS_FRONTEND_EVENT_RECORDING_STOPPING:
            {
                sem_post(data.capture_stats_sem);
                /*shm_unlink(MANGOHUD_OBS_STATS_SHM);*/
                data.recording = false;
                break;
            }

        default:
            break;
    }
}

bool obs_module_load(void)
{
    obs_frontend_add_event_callback(mangohud_obs_frontend_event_callback, NULL);
    obs_add_tick_callback(mangohud_obs_frontend_tick_callback, NULL);
    blog(LOG_INFO, "%s: plugin loaded successfully", MANGOHUD_OBS_NAME);

    return true;
}

void obs_module_unload()
{
    obs_output_release(data.output);
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
    return obs_module_text("Description");
}
