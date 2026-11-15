#include <sys/types.h>
#include <sys/ioctl.h>
#include <dev/acpica/acpiio.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <spdlog/spdlog.h>
#include "battery.h"
#include "sysctl_utils.h"

// Percentage, minutes left and charge state come from the hw.acpi.battery
// sysctls, which anyone can read and combine all batteries. The power draw is
// not exported there, it comes from the ACPI ioctl on /dev/acpi, like acpiconf(8).

static bool is_discharging()
{
    int state = 0;
    return sysctl_read("hw.acpi.battery.state", state) && state >= 0
        && (state & ACPI_BATT_STAT_DISCHARG);
}

void BatteryStats::numBattery()
{
    int units = 0;
    if (!sysctl_read("hw.acpi.battery.units", units) || units < 0)
        units = 0;

    batt_count = units < battery_count_max ? units : battery_count_max;
    batt_check = true;
}

void BatteryStats::update()
{
    if (!batt_check) {
        numBattery();
        if (batt_count == 0)
            SPDLOG_ERROR("No battery found");
    }

    if (batt_count <= 0)
        return;

    int state = 0;
    if (!sysctl_read("hw.acpi.battery.state", state))
        state = -1;

    const bool discharging = state >= 0 && (state & ACPI_BATT_STAT_DISCHARG);

    current_percent = getPercent();
    remaining_time = discharging ? getTimeRemaining() : 0.0f;
    current_watt = discharging ? getPower() : 0.0f;

    if (state >= 0 && (state & ACPI_BATT_STAT_CHARGING))
        current_status = "Charging";
    else if (discharging)
        current_status = "Discharging";
    else
        current_status = current_percent >= 100.0f ? "Full" : "Not charging";
}

float BatteryStats::getPercent()
{
    int life = -1; // percent, -1 when unknown
    if (!sysctl_read("hw.acpi.battery.life", life) || life < 0)
        return 0.0f;
    return static_cast<float>(life);
}

// Hours, like the Linux code
float BatteryStats::getTimeRemaining()
{
    int minutes = -1; // -1 when unknown
    if (!sysctl_read("hw.acpi.battery.time", minutes) || minutes <= 0 || !is_discharging())
        return 0.0f;
    return static_cast<float>(minutes) / 60.0f;
}

float BatteryStats::getPower()
{
    static int fd = -1;
    static bool tried = false;

    if (!tried) {
        tried = true;
        fd = open("/dev/acpi", O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            SPDLOG_WARN("battery: can't open /dev/acpi ({}), no power draw", strerror(errno));
    }
    if (fd < 0)
        return 0.0f;

    float watts = 0.0f;
    for (int unit = 0; unit < batt_count; unit++) {
        union acpi_battery_ioctl_arg io;

        io.unit = unit;
        if (ioctl(fd, ACPIIO_BATT_GET_BIX, &io) == -1)
            continue;
        const uint32_t power_units = io.bix.units;

        io.unit = unit;
        if (ioctl(fd, ACPIIO_BATT_GET_BST, &io) == -1)
            continue;
        // a charging unit reports its charge rate, only the draw counts
        if (io.bst.state == ACPI_BATT_STAT_NOT_PRESENT || !(io.bst.state & ACPI_BATT_STAT_DISCHARG)
            || io.bst.rate == ACPI_BATT_UNKNOWN)
            continue;

        if (power_units == ACPI_BIX_UNITS_MA) {
            if (io.bst.volt != ACPI_BATT_UNKNOWN)
                watts += static_cast<float>(io.bst.rate) * io.bst.volt / 1000000.0f; // mA * mV
        } else {
            watts += static_cast<float>(io.bst.rate) / 1000.0f; // mW
        }
    }

    return watts;
}

BatteryStats Battery_Stats;
