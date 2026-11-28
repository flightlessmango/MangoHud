#include "wrapper.hpp"

// Used where the Linux specific GPU backends (sysfs, procfs fdinfo, tracefs)
// can't work. Every metric reads as unavailable.

struct LegacyGPUWrapper::Impl {};

LegacyGPUWrapper::LegacyGPUWrapper(
    const std::string&, const std::string&, const std::string&, uint16_t, uint16_t
) : m_impl(std::make_unique<Impl>()) {}

LegacyGPUWrapper::~LegacyGPUWrapper() {}

void LegacyGPUWrapper::poll() {}
void LegacyGPUWrapper::add_pid(pid_t) {}

int   LegacyGPUWrapper::get_load() { return 0; }
float LegacyGPUWrapper::get_vram_used() { return 0; }
float LegacyGPUWrapper::get_gtt_used() { return 0; }
float LegacyGPUWrapper::get_memory_total() { return 0; }
int   LegacyGPUWrapper::get_memory_clock() { return 0; }
int   LegacyGPUWrapper::get_memory_temp() { return 0; }
int   LegacyGPUWrapper::get_temperature() { return 0; }
int   LegacyGPUWrapper::get_junction_temperature() { return 0; }
int   LegacyGPUWrapper::get_core_clock() { return 0; }
int   LegacyGPUWrapper::get_voltage() { return 0; }
float LegacyGPUWrapper::get_power_usage() { return 0; }
float LegacyGPUWrapper::get_power_limit() { return 0; }
bool  LegacyGPUWrapper::get_is_apu() { return false; }
float LegacyGPUWrapper::get_apu_cpu_power() { return 0; }
int   LegacyGPUWrapper::get_apu_cpu_temp() { return 0; }
bool  LegacyGPUWrapper::get_is_power_throttled() { return false; }
bool  LegacyGPUWrapper::get_is_current_throttled() { return false; }
bool  LegacyGPUWrapper::get_is_temp_throttled() { return false; }
bool  LegacyGPUWrapper::get_is_other_throttled() { return false; }
int   LegacyGPUWrapper::get_fan_speed() { return 0; }
bool  LegacyGPUWrapper::get_fan_rpm() { return false; }
int   LegacyGPUWrapper::get_process_load(pid_t) { return 0; }
float LegacyGPUWrapper::get_process_vram_used(pid_t) { return 0; }
float LegacyGPUWrapper::get_process_gtt_used(pid_t) { return 0; }

struct LegacyFDInfoWrapper::Impl {};

LegacyFDInfoWrapper::LegacyFDInfoWrapper(const std::string&)
    : m_impl(std::make_unique<Impl>()) {}

LegacyFDInfoWrapper::~LegacyFDInfoWrapper() {}

void  LegacyFDInfoWrapper::add_pid(pid_t) {}
void  LegacyFDInfoWrapper::poll_all() {}
float LegacyFDInfoWrapper::get_memory_used(pid_t, const std::string&) { return 0; }
