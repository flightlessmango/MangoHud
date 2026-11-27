#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>
#include "battery.hpp"
#include "../common/helpers.hpp"

namespace fs = std::filesystem;

static const fs::path deckard_dir = "/run/deckardcharger";
static const fs::path deckard_power_dir = "/run/power-monitor/power";
// About 12 s at the 500 ms poll interval
static constexpr size_t current_sample_count = 25;

static bool read_value(const fs::path& path, double& out) {
    const std::string line = read_line(path.string());
    if (line.empty())
        return false;

    try {
        out = std::stod(line);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

// A peripheral's battery (a gamepad, a mouse) has the scope "Device"
static bool is_system_battery(const fs::path& path) {
    return read_line((path / "type").string()) == "Battery" &&
           read_line((path / "scope").string()) != "Device";
}

void Battery::find() {
    std::vector<fs::path> found;

    for (const auto& dir : { fs::path("/sys/class/power_supply"), deckard_dir }) {
        const bool deckard = dir == deckard_dir;
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path& path = it->path();
            if (deckard ? path.filename().string().find("BAT") == std::string::npos : !is_system_battery(path))
                continue;

            // Skip duplicate battery symlinks
            std::error_code canonical_ec;
            const fs::path real = fs::canonical(path, canonical_ec);
            if (!canonical_ec && std::find(found.begin(), found.end(), real) != found.end())
                continue;

            found.push_back(real);
            paths.push_back(path);
        }
    }

    if (paths.empty())
        SPDLOG_DEBUG("No battery found");
}

float Battery::read_percent() const {
    double percent = 0;
    if (read_value(deckard_dir / "battery_percent", percent))
        return percent;

    double now = 0, full = 0;
    for (const auto& path : paths) {
        double n = 0, f = 0;
        if ((read_value(path / "charge_now", n) && read_value(path / "charge_full", f)) ||
            (read_value(path / "energy_now", n) && read_value(path / "energy_full", f))) {
            now += n;
            full += f;
        } else if (read_value(path / "capacity", n)) {
            // Without charge or energy counters, each battery counts the same
            now += n / 100;
            full += 1;
        }
    }

    // Some packs report a little over full right after charging
    return full > 0 ? std::min(now / full * 100, 100.0) : -1;
}

std::optional<battery_info> Battery::poll() {
    if (paths.empty())
        return std::nullopt;

    battery_info info;
    info.percent = read_percent();

    // The fuel gauge's own status flaps while held near full; deckardcharger publishes the combined one
    const std::string deckard_status = read_line((deckard_dir / "battery_status").string());
    bool discharging = false;
    double power = 0;   // µW
    double current = 0; // µA
    double charge = 0;  // µAh

    for (const auto& path : paths) {
        const std::string status = deckard_status.empty() ? read_line((path / "status").string()) : deckard_status;
        // A discharging battery wins, then a charging one: with two packs on AC, the idle one reports Full
        if (info.status.empty() || status == "Discharging" ||
            (status == "Charging" && info.status != "Discharging"))
            info.status = status;

        double voltage = 0, value = 0;
        const bool has_voltage = read_value(path / "voltage_now", voltage) && voltage > 0;

        if (read_value(path / "charge_now", value))
            charge += value;
        else if (has_voltage && read_value(path / "energy_now", value))
            charge += value / voltage * 1e6; // µWh / µV = Ah

        // Idle batteries report Not charging or Full, e.g. on dual-battery laptops
        if (status != "Discharging")
            continue;

        discharging = true;
        if (read_value(path / "power_now", value)) {
            power += std::fabs(value);
            if (has_voltage)
                current += std::fabs(value) / voltage * 1e6; // µW / µV = A
        } else if (read_value(path / "current_now", value)) {
            current += std::fabs(value);
            if (has_voltage)
                power += std::fabs(value) * voltage / 1e6;
        }
    }

    if (!discharging) {
        current_samples.clear();
        deckard_power = 0;
        return info;
    }

    std::error_code ec;
    if (fs::exists(deckard_power_dir / "vph", ec)) {
        // vph is the measured system rail, bl_power the backlight; keep the last value on a bad read
        double vph = 0, backlight = 0;
        if (read_value(deckard_power_dir / "vph", vph) && read_value(deckard_power_dir / "bl_power", backlight))
            deckard_power = vph + backlight;
        info.power = deckard_power;
    } else {
        info.power = power / 1e6;
    }

    if (current > 0) {
        current_samples.push_back(current);
        if (current_samples.size() > current_sample_count)
            current_samples.pop_front();
    }

    if (!current_samples.empty()) {
        double average = 0;
        for (double sample : current_samples)
            average += sample;
        average /= current_samples.size();
        info.time_remaining = charge / average;
    }

    return info;
}
