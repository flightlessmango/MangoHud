#pragma once

#include "../../hwmon.hpp"
#include "../cpu.hpp"

class RAPL : public CPUPower, private Hwmon {
private:
    const std::string rapl_path = "/sys/class/powercap/intel-rapl:0";
    const std::vector<hwmon_sensor> sensors = { { "energy", "energy_uj" } };
    struct Counter {
        uint64_t previous = 0;
        uint64_t max_range = 0;
        bool primed = false;
    };
    Counter package_counter;

    // RAPL uncore domain; its scope depends on the hardware.
    HwmonBase uncore;
    Counter uncore_counter;

    void find_uncore();
    float get_power(uint64_t total_usage, Counter& counter);

protected:
    void pre_poll_overrides() override;

public:
    RAPL();
    float get_power_usage() override;
    float get_uncore_power_usage() override;
    virtual ~RAPL() {};
};
