#include "obs_studio.h"
#include <cstdio>
#include <cstring>
#include <spdlog/spdlog.h>
#include "obs_shared.h"
#if __linux__
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#endif

ObsStats* ObsStudio::stats;
int ObsStudio::isinit;
int ObsStudio::islogged_obsunavailable;
char ObsStudio::col1[16];
char ObsStudio::col2[16];

void ObsStudio::atexit_func()
{
    if(!stats)
        return;

    stats->running_mangohud = 0;
    if(!stats->running_obs)
    {
#if __linux__
        char shmpath[1024];
        mangohud_obs_get_shmpath(shmpath, sizeof(shmpath));
        if(shm_unlink(shmpath) < 0)
            perror("shm_unlink");
#endif
    }
}

ObsStudio::ObsStudio(bool prefix_exe, const char* procname)
{
    isinit = -1;
#ifdef __linux__
    int fd;
    char shmpath[1024];
    mangohud_obs_get_shmpath(shmpath, sizeof(shmpath));
    if ((fd = shm_open(shmpath, O_CREAT | O_RDWR, 0666)) < 0)
    {
        SPDLOG_ERROR("shm_open {}", strerror(errno));
    }
    if(fd > 0 && ftruncate(fd, sizeof(ObsStats)) < 0)
    {
        SPDLOG_ERROR( "ftruncate {}", strerror(errno));
    }
    if ((stats = static_cast<ObsStats*>(mmap(nullptr, sizeof(ObsStats), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0))) == MAP_FAILED)
    {
        SPDLOG_ERROR( "mmap {}", strerror(errno));
    }else {
        if(atexit(atexit_func) != 0)
            SPDLOG_WARN("atexit failed! /dev/shm{} must be removed manually if you encounter issues with mangohud obs", shmpath);
        stats->prefix_exe = prefix_exe;
        stats->running_mangohud = 1;
        std::snprintf(stats->exe, sizeof(stats->exe),
                "%s", procname);
        isinit = 1;
    }
#endif
}

void ObsStudio::update()
{
    memset(col1, 0, sizeof(col1));
    memset(col2, 0, sizeof(col2));

    if(stats && stats->recording)
    {
        char mins_secs[6];
        time_t t = stats->time;
        struct tm* tm = gmtime(&t);
        strftime(mins_secs, sizeof(mins_secs), "%M:%S", tm);
        uint32_t hrs = t / 3600;
        std::snprintf(col1, sizeof(col1), "%u:%s", hrs, mins_secs);
        std::snprintf(col2, sizeof(col2), "%.1fMiB", stats->bytes / 1024.0 / 1024.0);
    }else if(stats && !stats->recording){
        const char* notrecordingstate = "Inactive";
        if(stats->running_obs)
            notrecordingstate = "Ready";
        std::snprintf(col1, sizeof(col1), "%s", notrecordingstate);
    }else{
        std::snprintf(col1, sizeof(col1), "%s", "Error");
    }
}
