#pragma once

#include "../../hwmon.hpp"
#include "../cpu.hpp"

class Zenergy : public CPUPower, private Hwmon {
private:
    const std::vector<hwmon_sensor> sensors = { { "energy", "energy1_input" } };
    uint64_t previous_usage = 0;

protected:
    void pre_poll_overrides() override;

public:
    Zenergy();
    float get_power_usage() override;
};
