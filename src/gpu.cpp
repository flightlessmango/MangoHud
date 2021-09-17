#include "gpu.h"
#include <inttypes.h>
#include <memory>
#include <functional>
#include <thread>
#include <cstring>
#include <spdlog/spdlog.h>
#include "nvctrl.h"
#include "timing.hpp"
#include "file_utils.h"
#ifdef HAVE_NVML
#include "nvidia_info.h"
#endif

#ifdef HAVE_LIBDRM_AMDGPU
#include "auth.h"
#include <xf86drm.h>
#include <libdrm/amdgpu_drm.h>
#include <libdrm/amdgpu.h>
#include <unistd.h>
#include <fcntl.h>
#endif

using namespace std::chrono_literals;

std::shared_ptr<GpuInfo> g_active_gpu;
std::unordered_map<std::string /*device*/, std::shared_ptr<struct GpuInfo>> g_gpu_infos;

bool NVCtrlInfo::init()
{
#ifdef HAVE_XNVCTRL
    // FIXME correct device index
    return checkXNVCtrl();
#else
    return false;
#endif
}

void NVCtrlInfo::update()
{
#ifdef HAVE_XNVCTRL
    if (nvctrlSuccess) {
        getNvctrlInfo();
        s.load = nvctrl_info.load;
        s.temp = nvctrl_info.temp;
        s.memory_used = nvctrl_info.memoryUsed / (1024.f);
        s.core_clock = nvctrl_info.CoreClock;
        s.memory_clock = nvctrl_info.MemClock;
        s.power_usage = 0;
        s.memory_total = nvctrl_info.memoryTotal;
        return;
    }
#endif
}

bool AMDGPUHWMonInfo::init()
{
    auto path  = sysfs_path + "/device";
    handles.busy = fopen((path + "/gpu_busy_percent").c_str(), "r");
    handles.vram_total = fopen((path + "/mem_info_vram_total").c_str(), "r");
    handles.vram_used = fopen((path + "/mem_info_vram_used").c_str(), "r");

    path += "/hwmon/";
    std::string tempFolder;
    if (find_folder(path, "hwmon", tempFolder)) {
        handles.core_clock = fopen((path + tempFolder + "/freq1_input").c_str(), "r");
        handles.memory_clock = fopen((path + tempFolder + "/freq2_input").c_str(), "r");
        handles.temp = fopen((path + tempFolder + "/temp1_input").c_str(), "r");
        handles.power_usage = fopen((path + tempFolder + "/power1_average").c_str(), "r");
    }

    return handles.busy && handles.temp && handles.vram_total && handles.vram_used;
}

void AMDGPUHWMonInfo::update()
{
    if (handles.busy) {
        rewind(handles.busy);
        fflush(handles.busy);
        int value = 0;
        if (fscanf(handles.busy, "%d", &value) != 1)
            value = 0;
        s.load = value;
    }

    if (handles.temp) {
        rewind(handles.temp);
        fflush(handles.temp);
        int value = 0;
        if (fscanf(handles.temp, "%d", &value) != 1)
            value = 0;
        s.temp = value / 1000;
    }

    int64_t value = 0;

    if (handles.vram_total) {
        rewind(handles.vram_total);
        fflush(handles.vram_total);
        if (fscanf(handles.vram_total, "%" PRId64, &value) != 1)
            value = 0;
        s.memory_total = float(value) / (1024 * 1024 * 1024);
    }

    if (handles.vram_used) {
        rewind(handles.vram_used);
        fflush(handles.vram_used);
        if (fscanf(handles.vram_used, "%" PRId64, &value) != 1)
            value = 0;
        s.memory_used = float(value) / (1024 * 1024 * 1024);
    }

    if (handles.core_clock) {
        rewind(handles.core_clock);
        fflush(handles.core_clock);
        if (fscanf(handles.core_clock, "%" PRId64, &value) != 1)
            value = 0;

        s.core_clock = value / 1000000;
    }

    if (handles.memory_clock) {
        rewind(handles.memory_clock);
        fflush(handles.memory_clock);
        if (fscanf(handles.memory_clock, "%" PRId64, &value) != 1)
            value = 0;

        s.memory_clock = value / 1000000;
    }

    if (handles.power_usage) {
        rewind(handles.power_usage);
        fflush(handles.power_usage);
        if (fscanf(handles.power_usage, "%" PRId64, &value) != 1)
            value = 0;

        s.power_usage = value / 1000000;
    }
}

#ifdef HAVE_LIBDRM_AMDGPU
#define DRM_ATLEAST_VERSION(ver, maj, min) \
    (ver->version_major > maj || (ver->version_major == maj && ver->version_minor >= min))

static int getgrbm_amdgpu(amdgpu_device_handle dev, uint32_t *out) {
    return amdgpu_read_mm_registers(dev, GRBM_STATUS / 4, 1,
                                    0xffffffff, 0, out);
}

struct amdgpu_handles : public gpu_handles
{
    amdgpu_device_handle dev;
    int fd;
    uint32_t version_major, version_minor, gui_percent {0};
    uint32_t ticks = 60, ticks_per_sec = 120;
    std::chrono::nanoseconds sleep_interval {};

    bool quit = false;
    std::thread collector;

    amdgpu_handles(amdgpu_device_handle dev_, int fd_, uint32_t major, uint32_t minor)
    : dev(dev_)
    , fd(fd_)
    , version_major(major)
    , version_minor(minor)
    {
        set_sampling_period(500000000 /* 500ms */);
        collector = std::thread(&amdgpu_handles::amdgpu_poll, this);
    }

    ~amdgpu_handles()
    {
        quit = true;
        if (collector.joinable())
            collector.join();
        amdgpu_device_deinitialize(dev);
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

    void amdgpu_poll()
    {
        uint32_t stat = 0, gui = 0, curr = 0;
        while (!quit)
        {
            getgrbm_amdgpu(dev, &stat);
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

void amdgpu_set_sampling_period(gpu_handles* dev, uint32_t period)
{
    auto amdgpu_dev = reinterpret_cast<amdgpu_handles*>(dev);
    if (amdgpu_dev)
        amdgpu_dev->set_sampling_period(period);
}

static amdgpu_handles* amdgpu_open(const char* path)
{
    int fd = open(path, O_RDWR | O_CLOEXEC);

    if (fd < 0) {
        SPDLOG_ERROR("Failed to open DRM device: {}", strerror(errno));
        return nullptr;
    }

    drmVersionPtr ver = drmGetVersion(fd);

    if (!ver) {
        SPDLOG_ERROR("Failed to query driver version: {}", strerror(errno));
        close(fd);
        return nullptr;
    }

    if (strcmp(ver->name, "amdgpu") || !DRM_ATLEAST_VERSION(ver, 3, 11)) {
        SPDLOG_ERROR("Unsupported driver/version: {} {}.{}.{}", ver->name, ver->version_major, ver->version_minor, ver->version_patchlevel);
        close(fd);
        drmFreeVersion(ver);
        return nullptr;
    }
    drmFreeVersion(ver);

    if (!authenticate_drm(fd)) {
        close(fd);
        return nullptr;
    }

    uint32_t drm_major, drm_minor;
    amdgpu_device_handle dev;
    if (amdgpu_device_initialize(fd, &drm_major, &drm_minor, &dev)){
        SPDLOG_ERROR("Failed to initialize amdgpu device: {}", strerror(errno));
        close(fd);
        return nullptr;
    }

    return new amdgpu_handles(dev, fd, drm_major, drm_minor);
}

bool AMDGPUInfo::init()
{
    int idx = -1;

    if (sscanf(sysfs_path.c_str(), "%*[^0-9]%d", &idx) != 1 || idx < 0)
        return false;

    const std::string dri_path = "/dev/dri/card" + std::to_string(idx);
    device = reinterpret_cast<gpu_handles*>(amdgpu_open(dri_path.c_str()));
    if (!device)
    {
        SPDLOG_WARN("Failed to open device '{}' with libdrm", dri_path);
        return false;
    }

    return true;
}


void AMDGPUInfo::update()
{
    uint64_t value = 0;
    uint32_t value32 = 0;
    auto amdgpu_dev = reinterpret_cast<amdgpu_handles*>(device);

    if (!amdgpu_dev || !DRM_ATLEAST_VERSION(amdgpu_dev, 3, 11))
        return;

    if (!amdgpu_query_info(amdgpu_dev->dev, AMDGPU_INFO_VRAM_USAGE, sizeof(uint64_t), &value))
        s.memory_used = float(value) / (1024 * 1024 * 1024);

    // FIXME probably not correct sensor
    if (!amdgpu_query_info(amdgpu_dev->dev, AMDGPU_INFO_MEMORY, sizeof(uint64_t), &value))
        s.memory_total = float(value) / (1024 * 1024 * 1024);

    if (!amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GFX_SCLK, sizeof(uint32_t), &value32))
        s.core_clock = value32;

    if (!amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GFX_MCLK, sizeof(uint32_t), &value32)) // XXX Doesn't work on APUs
        s.memory_clock = value32;

    //if (!amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GPU_LOAD, sizeof(uint32_t), &value32))
    //    load = value32;
    s.load = amdgpu_dev->gui_percent;

    if (!amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GPU_TEMP, sizeof(uint32_t), &value32))
        s.temp = value32 / 1000;

    if (!amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GPU_AVG_POWER, sizeof(uint32_t), &value32))
        s.power_usage = value32;
}
#endif
