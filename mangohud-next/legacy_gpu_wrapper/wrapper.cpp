#include "gpu/intel/i915/i915.hpp"
#include "gpu/intel/xe/xe.hpp"
#include "gpu/msm/dpu.hpp"
#include "gpu/msm/kgsl.hpp"
#include "gpu/panfrost.hpp"
#include "gpu/panthor.hpp"

#include "wrapper.hpp"

struct LegacyGPUWrapper::Impl {
    Impl(
        const std::string& driver, const std::string& drm_node, const std::string& pci_dev,
        uint16_t vendor_id, uint16_t device_id
    ) {
        if (driver == "i915") {
            gpu = std::make_unique<Intel_i915>(drm_node, pci_dev, vendor_id, device_id);
        } else if (driver == "xe") {
            gpu = std::make_unique<Intel_xe>(drm_node, pci_dev, vendor_id, device_id);
        } else if (driver == "msm_dpu") {
            gpu = std::make_unique<MSM_DPU>(drm_node, pci_dev, vendor_id, device_id);
        } else if (driver == "msm_drm") {
            gpu = std::make_unique<MSM_KGSL>(drm_node, pci_dev, vendor_id, device_id);
        } else if (driver == "panfrost") {
            gpu = std::make_unique<Panfrost>(drm_node, pci_dev, vendor_id, device_id);
        } else if (driver == "panthor") {
            gpu = std::make_unique<Panthor>(drm_node, pci_dev, vendor_id, device_id);
        }

        if (gpu) {
            gpu->start_thread_worker();
        } else {
            SPDLOG_WARN("Failed to construct gpu with driver {} on node {}", driver, drm_node);
        }
    }

    ~Impl() {}

    std::unique_ptr<GPU> gpu;

    std::map<pid_t, gpu_metrics_process_t> process_metrics;
    gpu_metrics_system_t system_metrics;
};

LegacyGPUWrapper::LegacyGPUWrapper(
    const std::string& driver, const std::string& drm_node, const std::string& pci_dev,
    uint16_t vendor_id, uint16_t device_id
) {
    m_impl = std::make_unique<Impl>(driver, drm_node, pci_dev, vendor_id, device_id);
}

LegacyGPUWrapper::~LegacyGPUWrapper() {

}

void LegacyGPUWrapper::poll() {
    m_impl->process_metrics = m_impl->gpu->get_process_metrics();
    m_impl->system_metrics = m_impl->gpu->get_system_metrics();
}

void LegacyGPUWrapper::add_pid(pid_t pid) {
    m_impl->gpu->add_pid(pid);

    if (auto* ptr = dynamic_cast<FDInfo*>(m_impl->gpu.get())) {
        ptr->fdinfo.add_pid(pid);
    }
}

#define GENERATE_SYS_METRIC_GETTER(name)                                                        \
    decltype(std::declval<LegacyGPUWrapper>().get_##name()) LegacyGPUWrapper::get_##name() {    \
        return m_impl->system_metrics.name;                                                     \
    }

#define GENERATE_PROC_METRIC_GETTER(name)                               \
    decltype(std::declval<LegacyGPUWrapper>().get_process_##name(0))    \
    LegacyGPUWrapper::get_process_##name(pid_t pid) {                   \
        return m_impl->process_metrics[pid].name;                       \
    }

GENERATE_SYS_METRIC_GETTER(load)
GENERATE_SYS_METRIC_GETTER(vram_used)
GENERATE_SYS_METRIC_GETTER(gtt_used)
GENERATE_SYS_METRIC_GETTER(memory_total)
GENERATE_SYS_METRIC_GETTER(memory_clock)
GENERATE_SYS_METRIC_GETTER(memory_temp)
GENERATE_SYS_METRIC_GETTER(temperature)
GENERATE_SYS_METRIC_GETTER(junction_temperature)
GENERATE_SYS_METRIC_GETTER(core_clock)
GENERATE_SYS_METRIC_GETTER(voltage)
GENERATE_SYS_METRIC_GETTER(power_usage)
GENERATE_SYS_METRIC_GETTER(power_limit)
GENERATE_SYS_METRIC_GETTER(is_apu)
GENERATE_SYS_METRIC_GETTER(apu_cpu_power)
GENERATE_SYS_METRIC_GETTER(apu_cpu_temp)
GENERATE_SYS_METRIC_GETTER(is_power_throttled)
GENERATE_SYS_METRIC_GETTER(is_current_throttled)
GENERATE_SYS_METRIC_GETTER(is_temp_throttled)
GENERATE_SYS_METRIC_GETTER(is_other_throttled)
GENERATE_SYS_METRIC_GETTER(fan_speed)
GENERATE_SYS_METRIC_GETTER(fan_rpm)

GENERATE_PROC_METRIC_GETTER(load)
GENERATE_PROC_METRIC_GETTER(vram_used)
GENERATE_PROC_METRIC_GETTER(gtt_used)

struct LegacyFDInfoWrapper::Impl {
    Impl(const std::string& drm_node) : fdinfo(drm_node) {}
    ~Impl() {}

    FDInfoWrapper fdinfo;
};

LegacyFDInfoWrapper::LegacyFDInfoWrapper(const std::string& drm_node) {
    m_impl = std::make_unique<Impl>(drm_node);
}

LegacyFDInfoWrapper::~LegacyFDInfoWrapper() {}

void LegacyFDInfoWrapper::add_pid(pid_t pid) {
    m_impl->fdinfo.add_pid(pid);
}

void LegacyFDInfoWrapper::poll_all() {
    m_impl->fdinfo.poll_all();
}

float LegacyFDInfoWrapper::get_memory_used(pid_t pid, const std::string& key) {
    return m_impl->fdinfo.get_memory_used(pid, key);
}
