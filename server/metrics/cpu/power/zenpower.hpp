#pragma once

#include "../../hwmon.hpp"
#include "../cpu.hpp"

class Zenpower : public CPUPower, private Hwmon {
private:
    const std::vector<hwmon_sensor> sensors = {
        // https://github.com/ocerman/zenpower/blob/5e2f56fabeba0c909edb90221eeace4a3d726dbb/zenpower.c#L490-L492
        // https://github.com/AliEmreSenel/zenpower3/blob/41e042935ee9840c0b9dd55d61b6ddd58bc4fde6/zenpower.c#L538-L540
        { "core_power", "power1_input" },
        { "soc_power" , "power2_input" }
    };

protected:
    void pre_poll_overrides() override;

public:
    Zenpower();
    float get_power_usage() override;
};
