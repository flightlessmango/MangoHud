#pragma once
#ifndef MANGOHUD_GPU_H
#define MANGOHUD_GPU_H

#include <cstdio>
#include <cstdint>
#include "overlay_params.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <regex>
#include <iostream>
#include "amdgpu.h"
#include "nvidia.h"
#include "gpu_metrics_util.h"
#include "gpu_fdinfo.h"

class GPU {
    private:
        std::string is_i915_or_xe();

    public:
        gpu_metrics metrics;
        std::string name;
        std::unique_ptr<NVIDIA> nvidia = nullptr;
        std::unique_ptr<AMDGPU> amdgpu = nullptr;
        std::unique_ptr<GPU_fdinfo> fdinfo = nullptr;
        bool is_active = false;
        std::string pci_dev;
        uint32_t vendor_id = 0;

        GPU(std::string name, uint32_t vendor_id, uint32_t device_id, const char* pci_dev)
            : name(name), pci_dev(pci_dev), vendor_id(vendor_id), device_id(device_id) {
                if (vendor_id == 0x10de)
                    nvidia = std::make_unique<NVIDIA>(pci_dev);

                if (vendor_id == 0x1002)
                    amdgpu = std::make_unique<AMDGPU>(pci_dev, device_id, vendor_id);

                // For now we're only accepting one of these modules at once
                // Might be possible that multiple can exist on a system in the future?
                if (vendor_id == 0x8086)
                    fdinfo = std::make_unique<GPU_fdinfo>(is_i915_or_xe(), pci_dev);

                if (vendor_id == 0x5143)
                    fdinfo = std::make_unique<GPU_fdinfo>("msm", pci_dev);
        }

        gpu_metrics get_metrics() {
            if (nvidia)
                this->metrics = nvidia->copy_metrics();

            if (amdgpu)
                this->metrics = amdgpu->copy_metrics();

            if (fdinfo)
                this->metrics = fdinfo->copy_metrics();

            return metrics;
        };

        std::vector<int> nvidia_pids() {
#ifdef HAVE_NVML
            if (nvidia)
                return nvidia->pids();
#endif
            return std::vector<int>();
        }

        void pause() {
            if (nvidia)
                nvidia->pause();
            
            if (amdgpu)
                amdgpu->pause();

            if (fdinfo)
                fdinfo->pause();
        }

        void resume() {
            if (nvidia)
                nvidia->resume();

            if (amdgpu)
                amdgpu->resume();

            if (fdinfo)
                fdinfo->resume();
        }

        bool is_apu() {
            if (amdgpu)
                return amdgpu->is_apu;
            else
                return false;
        }

        std::shared_ptr<Throttling> throttling() {
            if (nvidia)
                return nvidia->throttling;

            if (amdgpu)
                return amdgpu->throttling;

            return nullptr;
        }
        
        std::string gpu_text();
        std::string vram_text();

    private:
        uint32_t device_id;
        std::thread thread;

        int index_in_selected_gpus();
};

class GPUS {
    public:
        std::vector<std::shared_ptr<GPU>> available_gpus;
        std::mutex mutex;
        overlay_params* params;

        GPUS(overlay_params* params);

        void pause() {
            for (auto& gpu : available_gpus)
                gpu->pause();
        }

        void resume() {
            for (auto& gpu : available_gpus)
                gpu->resume();
        }

        std::shared_ptr<GPU> active_gpu() {
            if (available_gpus.empty())
                return nullptr;

            for (auto gpu : available_gpus) {
                if (gpu->is_active)
                    return gpu;
            }

            // if no GPU is marked as active, just set it to the last one
            // because integrated gpus are usually first
            return available_gpus.back();
        }

        void update_throttling() {
            for (auto gpu : available_gpus)
                if (gpu->throttling())
                    gpu->throttling()->update();
        }

        void get_metrics() {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto gpu : available_gpus)
                gpu->get_metrics();
        }

        std::vector<std::shared_ptr<GPU>> selected_gpus() {
            std::lock_guard<std::mutex> lock(mutex);
            std::vector<std::shared_ptr<GPU>> vec;

            if (params->gpu_list.empty() && params->pci_dev.empty())
                return available_gpus;

            if (!params->gpu_list.empty()) {
                for (unsigned index : params->gpu_list) {
                    if (index < available_gpus.size()) {
                        if (available_gpus[index])
                            vec.push_back(available_gpus[index]);
                    }
                }

                return vec;
            }

            if (!params->pci_dev.empty()) {
                for (auto &gpu : available_gpus) {
                    if (gpu->pci_dev == params->pci_dev) {
                        vec.push_back(gpu);
                        return vec;
                    }
                }

                return vec;
            }

            return vec;
        }

    private:
        std::string get_pci_device_address(const std::string& drm_card_path);
};

extern std::unique_ptr<GPUS> gpus;

void getNvidiaGpuInfo(const struct overlay_params& params);
void getAmdGpuInfo(void);
void getIntelGpuInfo();
bool checkNvidia(const char *pci_dev);
extern void nvapi_util();
extern bool checkNVAPI();
#endif //MANGOHUD_GPU_H
