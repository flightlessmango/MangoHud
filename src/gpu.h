#pragma once
#ifndef MANGOHUD_GPU_H
#define MANGOHUD_GPU_H

#include <cstdio>
#include <cstdint>
#include "overlay_params.h"
#include <unordered_map>
#include <memory>
#include <string>
#include <thread>

struct amdgpu_files
{
    FILE *vram_total;
    FILE *vram_used;
    /* The following can be NULL, in that case we're using the gpu_metrics node */
    FILE *busy;
    FILE *temp;
    FILE *core_clock;
    FILE *memory_clock;
    FILE *power_usage;
    FILE *gtt_used;
};

struct gpu_info {
    int load;
    int temp;
    uint64_t memory_used;
    uint64_t memory_total;
    int memory_clock;
    int core_clock;
    float power_usage;
    float apu_cpu_power;
    int apu_cpu_temp;
    bool is_power_throttled;
    bool is_current_throttled;
    bool is_temp_throttled;
    bool is_other_throttled;
    uint64_t gtt_used;
};

struct gpu_handles
{
    virtual ~gpu_handles() {};
};

struct gpu_device
{
    gpu_device(const std::string& sysfs, const std::string& pci)
    : sysfs_path(sysfs)
    , pci_device(pci)
    {}
    virtual ~gpu_device() {}
    virtual void update(const struct overlay_params& params) = 0;
    virtual bool init() = 0;

    std::string sysfs_path;
    std::string pci_device;
    std::string dev_name;
    gpu_info info {};
    uint32_t vendorID {}, deviceID {};
    gpu_handles* device {};
};

struct DummyGpu : public gpu_device
{
    DummyGpu() : gpu_device({}, {})
    {
        dev_name = "dummy";
    }
    void update(const struct overlay_params& params) {}
    bool init() { return true; }
};

struct NVMLInfo : public gpu_device
{
    NVMLInfo(const std::string& sysfs, const std::string& pci) : gpu_device(sysfs, pci) {}
    void update(const struct overlay_params& params);
    bool init();
};

struct NVCtrlInfo : public gpu_device
{
    NVCtrlInfo(const std::string& sysfs, const std::string& pci) : gpu_device(sysfs, pci) {}
    void update(const struct overlay_params& params);
    bool init();
};

struct NVAPIInfo : public gpu_device
{
    NVAPIInfo() : gpu_device({}, {}) {}
    void update(const struct overlay_params& params);
    bool init();
};

struct AMDGPUHWMonInfo : public gpu_device
{
    AMDGPUHWMonInfo(const std::string& sysfs, const std::string& pci) : gpu_device(sysfs, pci) {}
   virtual  ~AMDGPUHWMonInfo()
    {
        delete device;
        if (files.busy)
            fclose(files.busy);
        if (files.temp)
            fclose(files.temp);
        if (files.vram_total)
            fclose(files.vram_total);
        if (files.vram_used)
            fclose(files.vram_used);
        if (files.core_clock)
            fclose(files.core_clock);
        if (files.memory_clock)
            fclose(files.memory_clock);
        if (files.power_usage)
            fclose(files.power_usage);
        files = {};
    }

    virtual void update(const struct overlay_params& params);
    virtual bool init();

    amdgpu_files files {};
};

struct AMDGPUInfo : public AMDGPUHWMonInfo
{
    AMDGPUInfo(const std::string& metrics_, const std::string& sysfs, const std::string& pci)
    : AMDGPUHWMonInfo(sysfs, pci)
    , metrics_path(metrics_)
    {}

    ~AMDGPUInfo()
    {
        quit = true;
        if (thread.joinable())
            thread.join();
        if (file)
            fclose(file);
    }
    void update(const struct overlay_params& params);
    bool init();
private:
    void metrics_polling_thread();
    void get_instant_metrics(struct amdgpu_common_metrics& metrics);

    bool quit {false};
    FILE *file {};
    std::string metrics_path;
    std::thread thread;
};

extern std::shared_ptr<gpu_device> g_active_gpu;
extern std::unordered_map<std::string /*device*/, std::shared_ptr<gpu_device>> g_gpu_devices;

void getNvidiaGpuInfo(const struct overlay_params& params);
void getAmdGpuInfo(amdgpu_files& amdgpu, gpu_info& gpu_info, bool has_metrics = false);
bool checkNvidia(const char *pci_dev);
extern void nvapi_util();
extern bool checkNVAPI();
#endif //MANGOHUD_GPU_H
