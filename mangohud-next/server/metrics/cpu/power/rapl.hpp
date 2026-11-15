#pragma once

#include "../../hwmon.hpp"
#include "../cpu.hpp"

class RAPL : public CPUPower, private Hwmon {
private:
    const std::string rapl_path = "/sys/class/powercap/intel-rapl:0";
    const std::vector<hwmon_sensor> sensors = { { "energy", "energy_uj" } };
    uint64_t previous_usage = 0;

    // Uncore domain of the package. On CPUs with integrated graphics, most of it is the GPU.
    HwmonBase uncore;
    uint64_t previous_uncore_usage = 0;

    void find_uncore();
    float get_power(uint64_t total_usage, uint64_t& previous);

protected:
    void pre_poll_overrides() override;

public:
    RAPL();
    float get_power_usage() override;
    float get_uncore_power_usage() override;
    virtual ~RAPL() {};
};
