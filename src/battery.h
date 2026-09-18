#pragma once
#include <string>

static constexpr int battery_count_max = 2;

class BatteryStats{
    public:
        void numBattery();
        void update();
        float getPower();
        float getPercent();
        float getTimeRemaining();
        std::string battPath[battery_count_max];
        float current_watt = 0;
        float current_percent = 0;
        float remaining_time = 0;
        std::string current_status;
        std::string state[battery_count_max];
        int batt_count=0;
        bool batt_check = false;
        std::vector<float> current_now_vec = {};
};

extern BatteryStats Battery_Stats;
