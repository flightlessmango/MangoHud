#include <algorithm>
#include <vector>
#include <spdlog/spdlog.h>
#include <filesystem.h>
#include "battery.h"
#include "file_utils.h"
#include "string_utils.h"

namespace fs = ghc::filesystem;
using namespace std;

// A peripheral's battery (a gamepad, a mouse) has the scope "Device"
static bool is_system_battery(const fs::path& path) {
    return read_line((path / "type").string()) == "Battery" &&
           read_line((path / "scope").string()) != "Device";
}

void BatteryStats::numBattery() {
    std::error_code ec;
    int batteryCount = 0;
    std::vector<fs::path> found;

    for (const auto& path : { fs::path("/sys/class/power_supply/"), fs::path("/run/deckardcharger/") }) {
        if (!fs::exists(path, ec))
            continue;

        const bool is_deckard = path == fs::path("/run/deckardcharger/");

        std::error_code iterator_ec;
        for (fs::directory_iterator it(path, iterator_ec), end;
             !iterator_ec && it != end; it.increment(iterator_ec)) {
            auto& p = *it;
            string fileName = p.path().filename();
            if (batteryCount < battery_count_max &&
                (is_deckard ? fileName.find("BAT") != std::string::npos : is_system_battery(p.path()))) {
                // Skip duplicate battery symlinks
                std::error_code ec;
                fs::path real = fs::canonical(p.path(), ec);
                if (!ec && std::find(found.begin(), found.end(), real) != found.end())
                    continue;
                found.push_back(real);
                battPath[batteryCount] = p.path();
                batteryCount += 1;
            }
        }
        if (iterator_ec)
            SPDLOG_DEBUG("Failed to iterate {}: {}", fs::path(path).string(), iterator_ec.message());
    }

    batt_count = batteryCount;
    batt_check = true;
}

void BatteryStats::update() {
    if (!batt_check) {
        numBattery();
        if (batt_count == 0) {
            SPDLOG_ERROR("No battery found");
        }
    }

     if (batt_count > 0) {
        current_watt = getPower();
        current_percent = getPercent();
        remaining_time = getTimeRemaining();
    }
}

float BatteryStats::getPercent()
{
    std::error_code ec;
    float charge_n = 0;
    float charge_f = 0;

    std::string deckard_percent = read_line("/run/deckardcharger/battery_percent");
    if (!deckard_percent.empty()) {
        float percent = 0.0f;
        if (try_stof(percent, deckard_percent))
            return percent;
    }

    for(int i = 0; i < batt_count; i++) {
        string syspath = battPath[i];
        string charge_now = syspath + "/charge_now";
        string charge_full = syspath + "/charge_full";
        string energy_now = syspath + "/energy_now";
        string energy_full = syspath + "/energy_full";
        string capacity = syspath + "/capacity";

        if (fs::exists(charge_now, ec)) {
            std::ifstream input(charge_now);
            std::string line;
            if(std::getline(input, line)) {
                charge_n += (stof(line) / 1000000);
            }
            std::ifstream input2(charge_full);
            if(std::getline(input2, line)) {
                charge_f += (stof(line) / 1000000);
            }
        }

        else if (fs::exists(energy_now, ec)) {
            std::ifstream input(energy_now);
            std::string line;
            if(std::getline(input, line)) {
                charge_n += (stof(line) / 1000000);
            }
            std::ifstream input2(energy_full);
            if(std::getline(input2, line)) {
                charge_f += (stof(line) / 1000000);
            }
        }

        else {
            // using /sys/class/power_supply/BAT*/capacity
            // No way to get an accurate reading just average the percents if mutiple batteries
            std::ifstream input(capacity);
            std::string line;
            if(std::getline(input, line)) {
                charge_n += stof(line) / 100;
                charge_f = batt_count;
            }
        }
    }
    return (charge_n / charge_f) * 100;
}

// vph is the measured system rail, bl_power the backlight
static bool get_power_deckard(float& power) {
    std::string vph = read_line("/run/power-monitor/power/vph");
    std::string bl_power = read_line("/run/power-monitor/power/bl_power");
    if (vph.empty() || bl_power.empty())
        return false;

    float vph_w = 0.0f, bl_power_w = 0.0f;
    if (!try_stof(vph_w, vph) || !try_stof(bl_power_w, bl_power))
        return false;

    power = vph_w + bl_power_w;
    return true;
}

float BatteryStats::getPower() {
    std::error_code ec;
    float power_w = 0.0f;
    const bool deckard = fs::exists("/run/power-monitor/power/vph", ec);
    // the fuel gauge status flaps with the current sign while held near full
    const std::string deckard_status = read_line("/run/deckardcharger/battery_status");
    bool discharging = false;

    for (int i = 0; i < batt_count; i++) {
        string syspath = battPath[i];
        string current_now = syspath + "/current_now";
        string voltage_now = syspath + "/voltage_now";
        string power_now = syspath + "/power_now";
        string status = syspath + "/status";

        {
            std::string line = deckard_status.empty() ? read_line(status) : deckard_status;
            if (!line.empty()) {
                current_status = line;
                state[i] = current_status;
            }
        }

        // idle batteries report Not charging or Full, e.g. on dual-battery laptops
        if (state[i] != "Discharging")
            continue;

        discharging = true;
        if (deckard)
            continue;

        // Prefer power_now (µW) when available.
        if (fs::exists(power_now, ec)) {
            std::ifstream input(power_now);
            std::string line;
            if (std::getline(input, line)) {
                power_w += std::fabs(stof(line)) / 1000000.0f;
            }
            continue;
        }

        if (fs::exists(current_now, ec) && fs::exists(voltage_now, ec)) {
            float i_ua = 0.0f;
            float v_uv = 0.0f;

            {
                std::ifstream input(current_now);
                std::string line;
                if (std::getline(input, line)) {
                    i_ua = stof(line);
                }
            }
            {
                std::ifstream input(voltage_now);
                std::string line;
                if (std::getline(input, line)) {
                    v_uv = stof(line);
                }
            }

            power_w += (std::fabs(i_ua) * std::fabs(v_uv)) * 1e-12f;
        }
    }

    if (!discharging)
        return 0.0f;

    // Keep previous power on empty or invalid reads
    if (deckard)
        return get_power_deckard(power_w) ? power_w : current_watt;

    return power_w;
}

float BatteryStats::getTimeRemaining() {
    std::error_code ec;
    float current = 0.0f;
    float charge = 0.0f;

    for (int i = 0; i < batt_count; i++) {
        string syspath = battPath[i];
        string current_now = syspath + "/current_now";
        string charge_now = syspath + "/charge_now";
        string energy_now = syspath + "/energy_now";
        string voltage_now = syspath + "/voltage_now";
        string power_now = syspath + "/power_now";

        if (fs::exists(current_now, ec)) {
            std::ifstream input(current_now);
            std::string line;
            if (std::getline(input, line)) {
                current_now_vec.push_back(std::fabs(stof(line)));
            }
        } else if (fs::exists(power_now, ec) && fs::exists(voltage_now, ec)) {
            float voltage = 0.0f;
            float power = 0.0f;

            {
                std::ifstream input_voltage(voltage_now);
                std::string line;
                if (std::getline(input_voltage, line)) {
                    voltage = stof(line);
                }
            }
            {
                std::ifstream input_power(power_now);
                std::string line;
                if (std::getline(input_power, line)) {
                    power = stof(line);
                }
            }

            if (voltage > 0.0f) {
                // (µW / µV) = µA
                current_now_vec.push_back(std::fabs(power) / voltage);
            }
        }

        if (fs::exists(charge_now, ec)) {
            std::ifstream input(charge_now);
            std::string line;
            if (std::getline(input, line)) {
                charge += stof(line);
            }
        } else if (fs::exists(energy_now, ec) && fs::exists(voltage_now, ec)) {
            float energy = 0.0f;
            float voltage = 0.0f;

            {
                std::ifstream input_energy(energy_now);
                std::string line;
                if (std::getline(input_energy, line)) {
                    energy = stof(line);
                }
            }
            {
                std::ifstream input_voltage(voltage_now);
                std::string line;
                if (std::getline(input_voltage, line)) {
                    voltage = stof(line);
                }
            }

            if (voltage > 0.0f) {
                // (µWh / µV) = µAh
                charge += energy / voltage;
            }
        }

        if (current_now_vec.size() > 25) {
            current_now_vec.erase(current_now_vec.begin());
        }
    }

    if (current_now_vec.empty()) {
        return 0.0f;
    }

    for (const auto& current_now_sample : current_now_vec) {
        current += current_now_sample;
    }
    current /= static_cast<float>(current_now_vec.size());

    if (current <= 0.0f) {
        return 0.0f;
    }

    return charge / current;
}

BatteryStats Battery_Stats;
