#pragma once

#include <vector>
#include <fstream>
#include "../gpu/gpu_metrics.hpp"
#include "../hwmon.hpp"

class CPUPower {
protected:
    std::chrono::time_point<std::chrono::steady_clock> previous_time;
    std::chrono::nanoseconds delta_time_ns;

    bool _is_initialized = false;

    virtual void pre_poll_overrides() {};

public:
    void poll();
    bool is_initialized() { return _is_initialized; }

    virtual float get_power_usage() = 0;
    virtual ~CPUPower() = default;
};

class CPUTemp : private Hwmon {
private:
    struct cpu_temp_sensor {
        std::string name;
        hwmon_sensor sensor;
    };

    const std::vector<cpu_temp_sensor> sensors = {
        { "coretemp"       , { .filename = "temp\\d*_input", .label = "Package id 0"    } },
        { "zenpower"       , { .filename = "temp\\d*_input", .label = "T(die|ctl)"      } },
        { "k10temp"        , { .filename = "temp\\d*_input", .label = "T(die|ctl)"      } },
        { "atk0110"        , { .filename = "temp\\d*_input", .label = "CPU Temperature" } },
        { "it8603"         , { .filename = "temp\\d*_input", .label = "temp1"           } },
        { "cpuss0_.*"      , { .filename = "temp1_input"                                } },
        { "nct.*"          , { .filename = "temp\\d*_input", .label = "TSI0_TEMP"       } },
        { "asusec"         , { .filename = "temp\\d*_input", .label = "CPU"             } },
        { "l_pcs"          , { .filename = "temp\\d*_input", .label = "Node 0 Max"      } },
        { "cpu\\d*_thermal", { .filename = "temp1_input"                                } }
    };

    bool found_sensor = false;
public:
    void find_temperature_sensor();
    int get_temperature();
    virtual ~CPUTemp() {};
};

class CPU {
private:
    std::ifstream ifs_stat;
    std::ifstream ifs_cpuinfo;

    std::vector<uint64_t> prev_idle_times;
    std::vector<uint64_t> prev_total_times;

    void poll_load();
    void poll_frequency();
    void poll_power_usage();
    void poll_temperature();

    std::unique_ptr<CPUPower> init_power_usage();

    std::vector<std::vector<uint64_t>> get_cpu_times();
    bool get_cpu_times(
        const std::vector<uint64_t>& cpu_times, uint64_t &idle_time, uint64_t &total_time
    );

    std::unique_ptr<CPUPower> power_usage;
    CPUTemp temperature;

    cpu_info_t info;
    std::vector<core_info_t> cores;

public:
    CPU();

    void poll();
    virtual void pre_poll_overrides() {}
    cpu_info_t get_info();
    std::vector<core_info_t> get_core_info();
};
