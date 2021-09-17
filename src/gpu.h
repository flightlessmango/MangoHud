#pragma once
#ifndef MANGOHUD_GPU_H
#define MANGOHUD_GPU_H

#include <cstdio>
#include <cstdint>
#include <unordered_map>
#include <memory>
#include <string>

enum {
    GRBM_STATUS = 0x8010,
};

struct amdgpu_files
{
    FILE *busy;
    FILE *temp;
    FILE *vram_total;
    FILE *vram_used;
    FILE *core_clock;
    FILE *memory_clock;
    FILE *power_usage;
};

struct gpu_handles
{
    virtual ~gpu_handles() {};
};

struct GpuInfo
{
    GpuInfo(const std::string& sysfs, const std::string& pci)
    : sysfs_path(sysfs)
    , pci_device(pci)
    {}
    virtual ~GpuInfo() {}
    virtual void update() = 0;
    virtual bool init() = 0;

    std::string sysfs_path;
    std::string pci_device;
    std::string dev_name;
    bool inited;

    struct {
        int load;
        int temp;
        float memory_used;
        float memory_total;
        int memory_clock;
        int core_clock;
        int power_usage;
    } s {};

    uint32_t vendorID {}, deviceID {};
    gpu_handles* device {};
};

extern std::shared_ptr<GpuInfo> g_active_gpu;

struct NVMLInfo : public GpuInfo
{
    NVMLInfo(const std::string& sysfs, const std::string& pci) : GpuInfo(sysfs, pci) {}
    void update();
    bool init();
};

struct NVCtrlInfo : public GpuInfo
{
    NVCtrlInfo(const std::string& sysfs, const std::string& pci) : GpuInfo(sysfs, pci) {}
    void update();
    bool init();
};

struct NVAPIInfo : public GpuInfo
{
    NVAPIInfo() : GpuInfo({}, {}) {}
    void update();
    bool init();
};

struct AMDGPUInfo : public GpuInfo
{
    AMDGPUInfo(const std::string& sysfs, const std::string& pci) : GpuInfo(sysfs, pci) {}
    ~AMDGPUInfo()
    {
        delete device;
    }
    void update();
    bool init();
};

struct AMDGPUHWMonInfo : public GpuInfo
{
    AMDGPUHWMonInfo(const std::string& sysfs, const std::string& pci) : GpuInfo(sysfs, pci) {}
    ~AMDGPUHWMonInfo()
    {
        delete device;
        if (handles.busy)
            fclose(handles.busy);
        if (handles.temp)
            fclose(handles.temp);
        if (handles.vram_total)
            fclose(handles.vram_total);
        if (handles.vram_used)
            fclose(handles.vram_used);
        if (handles.core_clock)
            fclose(handles.core_clock);
        if (handles.memory_clock)
            fclose(handles.memory_clock);
        if (handles.power_usage)
            fclose(handles.power_usage);
        handles = {};
    }

    void update();
    bool init();

    amdgpu_files handles {};
};

struct RadeonInfo : public GpuInfo
{
    RadeonInfo(const std::string& sysfs, const std::string& pci) : GpuInfo(sysfs, pci) {}
    void update();
    bool init();
};

extern std::unordered_map<std::string /*device*/, std::shared_ptr<struct GpuInfo>> g_gpu_infos;

#ifdef HAVE_LIBDRM
void radeon_set_sampling_period(gpu_handles* dev, uint32_t period);
#endif
#ifdef HAVE_LIBDRM_AMDGPU
void amdgpu_set_sampling_period(gpu_handles* dev, uint32_t period);
#endif
bool checkNvidia(const char *pci_dev);
extern void nvapi_util();
extern bool checkNVAPI();
#endif //MANGOHUD_GPU_H
