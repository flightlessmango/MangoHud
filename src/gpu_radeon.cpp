#include <spdlog/spdlog.h>
#include "gpu.h"
#include <thread>
#include <chrono>
#include <radeon_drm.h>
#include <xf86drm.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include "auth.h"

using namespace std::chrono_literals;

#ifndef RADEON_INFO_VRAM_USAGE
#define RADEON_INFO_VRAM_USAGE 0x1e
#endif
#ifndef RADEON_INFO_READ_REG
#define RADEON_INFO_READ_REG 0x24
#endif
#ifndef RADEON_INFO_CURRENT_GPU_SCLK
#define RADEON_INFO_CURRENT_GPU_SCLK 0x22
#endif
#ifndef RADEON_INFO_CURRENT_GPU_MCLK
#define RADEON_INFO_CURRENT_GPU_MCLK 0x23
#endif

static const char* get_enum_str(uint32_t request)
{
    switch (request)
    {
        case RADEON_INFO_READ_REG: return "RADEON_INFO_READ_REG";
        case RADEON_INFO_VRAM_USAGE: return "RADEON_INFO_VRAM_USAGE";
        case RADEON_INFO_CURRENT_GPU_SCLK: return "RADEON_INFO_CURRENT_GPU_SCLK";
        case RADEON_INFO_CURRENT_GPU_MCLK: return "RADEON_INFO_CURRENT_GPU_MCLK";
        default: break;
    }
    return "Unknown";
}

#define DRM_ATLEAST_VERSION(ver, maj, min) \
    (ver->version_major > maj || (ver->version_major == maj && ver->version_minor >= min))

static int get_radeon_drm_value(int fd, uint32_t request, void *out)
{
    struct drm_radeon_info info {};
    info.value = reinterpret_cast<uint64_t>(out);
    info.request = request;

    // Or ioctl(fd, DRM_IOCTL_RADEON_INFO, &info);
    int ret = drmCommandWriteRead(fd, DRM_RADEON_INFO, &info, sizeof(info));
    if (ret)
        SPDLOG_ERROR("radeon drm error: {}", get_enum_str(request));
    return ret;
}

struct radeon_handles
{
    int fd;
    uint32_t version_major, version_minor, gui_percent {0};
    uint32_t ticks = 60, ticks_per_sec = 120;
    std::chrono::nanoseconds sleep_interval {};

    bool quit = false;
    std::thread collector;

    radeon_handles(int fd_, uint32_t major, uint32_t minor)
    : fd(fd_)
    , version_major(major)
    , version_minor(minor)
    {
        set_sampling_period(500000000 /* 500ms */);
        collector = std::thread(&radeon_handles::poll, this);
    }

    ~radeon_handles()
    {
        quit = true;
        if (collector.joinable())
            collector.join();
        close(fd);
    }

    void set_sampling_period(uint32_t period)
    {
        if (period < 10000000)
            period = 10000000; /* 10ms */
        ticks = ticks_per_sec * std::chrono::nanoseconds(period) / 1s;
        sleep_interval = std::chrono::nanoseconds(period) / ticks;
        SPDLOG_DEBUG("ticks: {}, {}ns", ticks, sleep_interval.count());
    }

    void poll()
    {
        uint32_t stat = 0, gui = 0, curr = 0;
        while (!quit)
        {
            stat = GRBM_STATUS;
            if (get_radeon_drm_value(fd, RADEON_INFO_READ_REG, &stat))
            {
                gui_percent = 0;
                curr = 0;
                gui = 0;
                std::this_thread::sleep_for(sleep_interval * ticks);
                continue;
            }

            if (stat & (1U << 31)) // gui
                gui++;

            std::this_thread::sleep_for(sleep_interval);
            curr++;
            curr %= ticks;
            if (!curr)
            {
                gui_percent = gui * 100 / ticks;
                gui = 0;
            }
        }
    }
};

typedef std::unique_ptr<radeon_handles> radeon_ptr;
static radeon_ptr radeon_dev;

void radeon_set_sampling_period(uint32_t period)
{
    if (radeon_dev)
        radeon_dev->set_sampling_period(period);
}

bool radeon_open(const char *path)
{
    uint32_t drm_major = 0, drm_minor = 0;

    int fd = open(path, O_RDWR | O_CLOEXEC);

    if (fd < 0) {
        SPDLOG_ERROR("Failed to open DRM device: {}", strerror(errno));
        return false;
    }

    drmVersionPtr ver = drmGetVersion(fd);

    if (!ver) {
        SPDLOG_ERROR("Failed to query driver version: {}", strerror(errno));
        close(fd);
        return false;
    }

    if (strcmp(ver->name, "radeon") || !DRM_ATLEAST_VERSION(ver, 2, 42)) {
        SPDLOG_ERROR("Unsupported driver/version: {} {}.{}.{}",
                     ver->name, ver->version_major, ver->version_minor, ver->version_patchlevel);
        close(fd);
        drmFreeVersion(ver);
        return false;
    }

    drm_major = ver->version_major;
    drm_minor = ver->version_minor;
    drmFreeVersion(ver);

    if (!authenticate_drm(fd)) {
        close(fd);
        return false;
    }

    radeon_dev = std::make_unique<radeon_handles>(fd, drm_major, drm_minor);
    return true;
}

void getRadeonInfo_libdrm()
{
    uint64_t value = 0;
    uint32_t value32 = 0;

    if (!radeon_dev)
        return;

    gpu_info.load = radeon_dev->gui_percent;

    // TODO one shot?
    struct drm_radeon_gem_info buffer {};
    int ret = 0;
    if (!(ret = ioctl(radeon_dev->fd, DRM_IOCTL_RADEON_GEM_INFO, &buffer)))
        gpu_info.memoryTotal = buffer.vram_size / (1024.f * 1024.f * 1024.f);
    else
        SPDLOG_ERROR("DRM_IOCTL_RADEON_GEM_INFO failed: {}", ret);

    if (!get_radeon_drm_value(radeon_dev->fd, RADEON_INFO_VRAM_USAGE, &value))
        gpu_info.memoryUsed = value / (1024.f * 1024.f * 1024.f);

    if (!get_radeon_drm_value(radeon_dev->fd, RADEON_INFO_CURRENT_GPU_SCLK, &value32))
        gpu_info.CoreClock = value32;

    if (!get_radeon_drm_value(radeon_dev->fd, RADEON_INFO_CURRENT_GPU_MCLK, &value32))
        gpu_info.MemClock = value32;

    if (!get_radeon_drm_value(radeon_dev->fd, RADEON_INFO_CURRENT_GPU_TEMP, &value32))
        gpu_info.temp = value32 / 1000;

    gpu_info.powerUsage = 0;
}
