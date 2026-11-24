#pragma once

#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct battery_info {
    // -1 when no counter could be read
    float percent = -1.f;
    std::string status;
    // Only known while discharging, 0 otherwise
    float power = 0.f;          // W
    float time_remaining = 0.f; // hours
};

class Battery {
public:
    Battery() { find(); }

    // std::nullopt when the system has no battery
    std::optional<battery_info> poll();

private:
    std::vector<std::filesystem::path> paths;
    std::deque<double> current_samples; // µA
    float deckard_power = 0.f;

    void find();
    float read_percent() const;
};
