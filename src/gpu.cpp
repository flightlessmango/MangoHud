#include "gpu.h"
#include <inttypes.h>
#include <memory>
#include <functional>
#include <thread>
#include <cstring>
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

void getNvidiaGpuInfo(){
#ifdef HAVE_NVML
    if (nvmlSuccess){
        getNVMLInfo();
        gpu_info.load = nvidiaUtilization.gpu;
        gpu_info.temp = nvidiaTemp;
        gpu_info.memoryUsed = nvidiaMemory.used / (1024.f * 1024.f * 1024.f);
        gpu_info.CoreClock = nvidiaCoreClock;
        gpu_info.MemClock = nvidiaMemClock;
        gpu_info.powerUsage = nvidiaPowerUsage / 1000;
        gpu_info.memoryTotal = nvidiaMemory.total / (1024.f * 1024.f * 1024.f);
        return;
    }
#endif
#ifdef HAVE_XNVCTRL
    if (nvctrlSuccess) {
        getNvctrlInfo();
        gpu_info.load = nvctrl_info.load;
        gpu_info.temp = nvctrl_info.temp;
        gpu_info.memoryUsed = nvctrl_info.memoryUsed / (1024.f);
        gpu_info.CoreClock = nvctrl_info.CoreClock;
        gpu_info.MemClock = nvctrl_info.MemClock;
        gpu_info.powerUsage = 0;
        gpu_info.memoryTotal = nvctrl_info.memoryTotal;
        return;
    }
#endif
#ifdef _WIN32
nvapi_util();
#endif
}

void getAmdGpuInfo(){
    if (amdgpu.busy) {
        rewind(amdgpu.busy);
        fflush(amdgpu.busy);
        int value = 0;
        if (fscanf(amdgpu.busy, "%d", &value) != 1)
            value = 0;
        gpu_info.load = value;
    }

    if (amdgpu.temp) {
        rewind(amdgpu.temp);
        fflush(amdgpu.temp);
        int value = 0;
        if (fscanf(amdgpu.temp, "%d", &value) != 1)
            value = 0;
        gpu_info.temp = value / 1000;
    }

    int64_t value = 0;

    if (amdgpu.vram_total) {
        rewind(amdgpu.vram_total);
        fflush(amdgpu.vram_total);
        if (fscanf(amdgpu.vram_total, "%" PRId64, &value) != 1)
            value = 0;
        gpu_info.memoryTotal = float(value) / (1024 * 1024 * 1024);
    }

    if (amdgpu.vram_used) {
        rewind(amdgpu.vram_used);
        fflush(amdgpu.vram_used);
        if (fscanf(amdgpu.vram_used, "%" PRId64, &value) != 1)
            value = 0;
        gpu_info.memoryUsed = float(value) / (1024 * 1024 * 1024);
    }

    if (amdgpu.core_clock) {
        rewind(amdgpu.core_clock);
        fflush(amdgpu.core_clock);
        if (fscanf(amdgpu.core_clock, "%" PRId64, &value) != 1)
            value = 0;

        gpu_info.CoreClock = value / 1000000;
    }

    if (amdgpu.memory_clock) {
        rewind(amdgpu.memory_clock);
        fflush(amdgpu.memory_clock);
        if (fscanf(amdgpu.memory_clock, "%" PRId64, &value) != 1)
            value = 0;

        gpu_info.MemClock = value / 1000000;
    }

    if (amdgpu.power_usage) {
        rewind(amdgpu.power_usage);
        fflush(amdgpu.power_usage);
        if (fscanf(amdgpu.power_usage, "%" PRId64, &value) != 1)
            value = 0;

        gpu_info.powerUsage = value / 1000000;
    }
}

#ifdef HAVE_LIBDRM_AMDGPU
#define DRM_ATLEAST_VERSION(ver, maj, min) \
    (ver->version_major > maj || (ver->version_major == maj && ver->version_minor >= min))

enum {
    GRBM_STATUS = 0x8010,
};

static std::unique_ptr<libdrm_loader> libdrm_ptr;

static int getgrbm_amdgpu(amdgpu_device_handle dev, uint32_t *out) {
    return libdrm_ptr->amdgpu_read_mm_registers(dev, GRBM_STATUS / 4, 1,
                                    0xffffffff, 0, out);
}

struct amdgpu_handles
{
    amdgpu_device_handle dev;
    int fd;
    uint32_t version_major, version_minor, gui_percent {0};
    const uint32_t ticks = 120;
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
        libdrm_ptr->amdgpu_device_deinitialize(dev);
        close(fd);
    }

    void set_sampling_period(uint32_t period)
    {
        sleep_interval = std::chrono::nanoseconds(period) / ticks;
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
    if (!libdrm_ptr)
        libdrm_ptr = std::make_unique<libdrm_loader>();

    if (!libdrm_ptr->IsLoaded())
        return false;

    int fd = open(path, O_RDWR | O_CLOEXEC);

    if (fd < 0) {
        perror("MANGOHUD: Failed to open DRM device: "); // Gives sensible perror message?
        return false;
    }

    drmVersionPtr ver = libdrm_ptr->drmGetVersion(fd);

    if (!ver) {
        perror("MANGOHUD: Failed to query driver version: ");
        close(fd);
        return false;
    }

    if (strcmp(ver->name, "amdgpu") || !DRM_ATLEAST_VERSION(ver, 3, 11)) {
        fprintf(stderr, "MANGOHUD: Unsupported driver/version: %s %d.%d.%d\n", ver->name, ver->version_major, ver->version_minor, ver->version_patchlevel);
        close(fd);
        libdrm_ptr->drmFreeVersion(ver);
        return false;
    }
    libdrm_ptr->drmFreeVersion(ver);

/*
    if (!authenticate_drm(fd)) {
        close(fd);
        return false;
    }
*/

    uint32_t drm_major, drm_minor;
    amdgpu_device_handle dev;
    if (libdrm_ptr->amdgpu_device_initialize(fd, &drm_major, &drm_minor, &dev)){
        perror("MANGOHUD: Failed to initialize amdgpu device");
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

    if (!DRM_ATLEAST_VERSION(amdgpu_dev, 3, 11))
    {
        getAmdGpuInfo();
        getAmdGpuInfo_actual = getAmdGpuInfo;
        return;
    }

    if (!libdrm_ptr || !libdrm_ptr->IsLoaded())
        return;

    if (!libdrm_ptr->amdgpu_query_info(amdgpu_dev->dev, AMDGPU_INFO_VRAM_USAGE, sizeof(uint64_t), &value))
        gpu_info.memoryUsed = float(value) / (1024 * 1024 * 1024);

    // FIXME probably not correct sensor
    if (!libdrm_ptr->amdgpu_query_info(amdgpu_dev->dev, AMDGPU_INFO_MEMORY, sizeof(uint64_t), &value))
        gpu_info.memoryTotal = float(value) / (1024 * 1024 * 1024);

    if (!libdrm_ptr->amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GFX_SCLK, sizeof(uint32_t), &value32))
        gpu_info.CoreClock = value32;

    if (!libdrm_ptr->amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GFX_MCLK, sizeof(uint32_t), &value32)) // XXX Doesn't work on APUs
        gpu_info.MemClock = value32;

    //if (!libdrm_ptr->amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GPU_LOAD, sizeof(uint32_t), &value32))
    //    gpu_info.load = value32;
    gpu_info.load = amdgpu_dev->gui_percent;

    if (!libdrm_ptr->amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GPU_TEMP, sizeof(uint32_t), &value32))
        gpu_info.temp = value32 / 1000;

    if (!libdrm_ptr->amdgpu_query_sensor_info(amdgpu_dev->dev, AMDGPU_INFO_SENSOR_GPU_AVG_POWER, sizeof(uint32_t), &value32))
        gpu_info.powerUsage = value32;
}
#endif
