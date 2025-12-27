#pragma once

#include "../../hwmon.hpp"
#include "../cpu.hpp"

class RAPL : public CPUPower, private Hwmon {
private:
    const std::string rapl_path = "/sys/class/powercap/intel-rapl:0";
    const std::vector<hwmon_sensor> sensors = { { "energy", "energy_uj" } };
    uint64_t previous_usage = 0;

protected:
    void pre_poll_overrides() override;

public:
    RAPL();
    float get_power_usage() override;
};
