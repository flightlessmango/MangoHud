#include "gpu.h"
#include <inttypes.h>
#include <memory>
#include <functional>
#include <thread>
#include <cstring>
#include <spdlog/spdlog.h>
#include "nvctrl.h"
#include "timing.hpp"
#ifdef HAVE_NVML
#include "nvidia_info.h"
#endif

#ifdef HAVE_LIBDRM_AMDGPU
//#include "auth.h"
#include <xf86drm.h>
#include <libdrm/amdgpu_drm.h>
#include <libdrm/amdgpu.h>
#include <unistd.h>
#include <fcntl.h>
#include "loaders/loader_libdrm.h"
#endif

using namespace std::chrono_literals;

struct gpuInfo gpu_info {};
amdgpu_files amdgpu {};
decltype(&getAmdGpuInfo) getAmdGpuInfo_actual = nullptr;

bool checkNvidia(const char *pci_dev){
    bool nvSuccess = false;
#ifdef HAVE_NVML
    nvSuccess = checkNVML(pci_dev) && getNVMLInfo();
#endif
#ifdef HAVE_XNVCTRL
    if (!nvSuccess)
        nvSuccess = checkXNVCtrl();
#endif
#ifdef _WIN32
    if (!nvSuccess)
        nvSuccess = checkNVAPI();
#endif
    return nvSuccess;
}

void getNvidiaGpuInfo(int32_t deviceID){
#ifdef HAVE_NVML
    if (nvmlSuccess){
        getNVMLInfo();
        gpu_info[deviceID].load = nvidiaUtilization.gpu;
        gpu_info[deviceID].temp = nvidiaTemp;
        gpu_info[deviceID].memoryUsed = nvidiaMemory.used / (1024.f * 1024.f * 1024.f);
        gpu_info[deviceID].CoreClock = nvidiaCoreClock;
        gpu_info[deviceID].MemClock = nvidiaMemClock;
        gpu_info[deviceID].powerUsage = nvidiaPowerUsage / 1000;
        gpu_info[deviceID].memoryTotal = nvidiaMemory.total / (1024.f * 1024.f * 1024.f);
        return;
    }
#endif
#ifdef HAVE_XNVCTRL
    if (nvctrlSuccess) {
        getNvctrlInfo();
        gpu_info[deviceID].load = nvctrl_info.load;
        gpu_info[deviceID].temp = nvctrl_info.temp;
        gpu_info[deviceID].memoryUsed = nvctrl_info.memoryUsed / (1024.f);
        gpu_info[deviceID].CoreClock = nvctrl_info.CoreClock;
        gpu_info[deviceID].MemClock = nvctrl_info.MemClock;
        gpu_info[deviceID].powerUsage = 0;
        gpu_info[deviceID].memoryTotal = nvctrl_info.memoryTotal;
        return;
    }
#endif
#ifdef _WIN32
nvapi_util();
#endif
}

void getAmdGpuInfo(int32_t deviceID){
  if (amdgpu[deviceID].busy) {
      rewind(amdgpu[deviceID].busy);
      fflush(amdgpu[deviceID].busy);
      int value = 0;
      if (fscanf(amdgpu[deviceID].busy, "%d", &value) != 1)
          value = 0;
      gpu_info[deviceID].load = value;
  }

  if (amdgpu[deviceID].temp) {
      rewind(amdgpu[deviceID].temp);
      fflush(amdgpu[deviceID].temp);
      int value = 0;
      if (fscanf(amdgpu[deviceID].temp, "%d", &value) != 1)
          value = 0;
      gpu_info[deviceID].temp = value / 1000;
  }

  int64_t value = 0;

  if (amdgpu[deviceID].vram_total) {
      rewind(amdgpu[deviceID].vram_total);
      fflush(amdgpu[deviceID].vram_total);
      if (fscanf(amdgpu[deviceID].vram_total, "%" PRId64, &value) != 1)
            value = 0;
        gpu_info[deviceID].memoryTotal = float(value) / (1024 * 1024 * 1024);
    }

    if (amdgpu[deviceID].vram_used) {
        rewind(amdgpu[deviceID].vram_used);
        fflush(amdgpu[deviceID].vram_used);
        if (fscanf(amdgpu[deviceID].vram_used, "%" PRId64, &value) != 1)
            value = 0;
        gpu_info[deviceID].memoryUsed = float(value) / (1024 * 1024 * 1024);
    }

    if (amdgpu[deviceID].core_clock) {
        rewind(amdgpu[deviceID].core_clock);
        fflush(amdgpu[deviceID].core_clock);
        if (fscanf(amdgpu[deviceID].core_clock, "%" PRId64, &value) != 1)
            value = 0;

        gpu_info[deviceID].CoreClock = value / 1000000;
    }

    if (amdgpu[deviceID].memory_clock) {
        rewind(amdgpu[deviceID].memory_clock);
        fflush(amdgpu[deviceID].memory_clock);
        if (fscanf(amdgpu[deviceID].memory_clock, "%" PRId64, &value) != 1)
            value = 0;

        gpu_info[deviceID].MemClock = value / 1000000;
    }

    if (amdgpu[deviceID].power_usage) {
        rewind(amdgpu[deviceID].power_usage);
        fflush(amdgpu[deviceID].power_usage);
        if (fscanf(amdgpu[deviceID].power_usage, "%" PRId64, &value) != 1)
            value = 0;

        gpu_info[deviceID].powerUsage = value / 1000000;
    }
}

#ifdef HAVE_LIBDRM_AMDGPU
#define DRM_ATLEAST_VERSION(ver, maj, min) \
    (ver->version_major > maj || (ver->version_major == maj && ver->version_minor >= min))

enum {
    GRBM_STATUS = 0x8010,
};

static std::unique_ptr<libdrm_amdgpu_loader> libdrm_amdgpu_ptr;

static int getgrbm_amdgpu(amdgpu_device_handle dev, uint32_t *out) {
    return libdrm_amdgpu_ptr->amdgpu_read_mm_registers(dev, GRBM_STATUS / 4, 1,
                                    0xffffffff, 0, out);
}

struct amdgpu_handles
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
        libdrm_amdgpu_ptr->amdgpu_device_deinitialize(dev);
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

typedef std::unique_ptr<amdgpu_handles> amdgpu_ptr;
static amdgpu_ptr amdgpu_dev;

void amdgpu_set_sampling_period(uint32_t period)
{
    if (amdgpu_dev)
        amdgpu_dev->set_sampling_period(period);
}

bool amdgpu_open(const char *path) {
    if (!g_libdrm.IsLoaded())
        return false;

    if (!libdrm_amdgpu_ptr)
        libdrm_amdgpu_ptr = std::make_unique<libdrm_amdgpu_loader>();

    if (!libdrm_amdgpu_ptr->IsLoaded())
        return false;

    int fd = open(path, O_RDWR | O_CLOEXEC);

    if (fd < 0) {
        SPDLOG_ERROR("Failed to open DRM device: {}", strerror(errno));
        return false;
    }

    drmVersionPtr ver = g_libdrm.drmGetVersion(fd);

    if (!ver) {
        SPDLOG_ERROR("Failed to query driver version: {}", strerror(errno));
        close(fd);
        return false;
    }

    if (strcmp(ver->name, "amdgpu") || !DRM_ATLEAST_VERSION(ver, 3, 11)) {
        SPDLOG_ERROR("Unsupported driver/version: {} {}.{}.{}", ver->name, ver->version_major, ver->version_minor, ver->version_patchlevel);
        close(fd);
        g_libdrm.drmFreeVersion(ver);
        return false;
    }
    g_libdrm.drmFreeVersion(ver);

/*
    if (!authenticate_drm(fd)) {
        close(fd);
        return false;
    }
*/

    uint32_t drm_major, drm_minor;
    amdgpu_device_handle dev;
    if (libdrm_amdgpu_ptr->amdgpu_device_initialize(fd, &drm_major, &drm_minor, &dev)){
        SPDLOG_ERROR("Failed to initialize amdgpu device: {}", strerror(errno));
        close(fd);
        return false;
    }

    amdgpu_dev = std::make_unique<amdgpu_handles>(dev, fd, drm_major, drm_minor);
    return true;
}

void getAmdGpuInfo_libdrm()
{
    uint64_t value = 0;
    uint32_t value32 = 0;

    if (!amdgpu_dev || !DRM_ATLEAST_VERSION(amdgpu_dev, 3, 11))
    {
        getAmdGpuInfo();
        getAmdGpuInfo_actual = getAmdGpuInfo;
        return;
    }

    if (!libdrm_amdgpu_ptr->amdgpu_query_info(amdgpu_dev->dev, AMDGPU_INFO_VRAM_USAGE, sizeof(uint64_t), &value))
        gpu_info.memoryUsed = float(value) / (1024 * 1024 * 1024);

    // FIXME probably not correct sensor
    if (!libdrm_amdgpu_ptr->amdgpu_query_info(amdgpu_dev->dev, AMDGPU_INFO_MEMORY, sizeof(uint64_t), &value))
        gpu_info.memoryTotal = float(value) / (1024 * 1024 * 1024);

    if (!libdrm_amdgpu_ptr->amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GFX_SCLK, sizeof(uint32_t), &value32))
        gpu_info.CoreClock = value32;

    if (!libdrm_amdgpu_ptr->amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GFX_MCLK, sizeof(uint32_t), &value32)) // XXX Doesn't work on APUs
        gpu_info.MemClock = value32;

    //if (!libdrm_amdgpu_ptr->amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GPU_LOAD, sizeof(uint32_t), &value32))
    //    gpu_info.load = value32;
    gpu_info.load = amdgpu_dev->gui_percent;

    if (!libdrm_amdgpu_ptr->amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GPU_TEMP, sizeof(uint32_t), &value32))
        gpu_info.temp = value32 / 1000;

    if (!libdrm_amdgpu_ptr->amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GPU_AVG_POWER, sizeof(uint32_t), &value32))
        gpu_info.powerUsage = value32;
}
#endif
