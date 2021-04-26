#pragma once
#include "overlay.h"
#include "overlay_params.h"
#include <logging.h>
#include <vector>
#include <unordered_map>
#include <filesystem>

class BatteryStats{
    public:
        int numBattery();
        void findFiles();
        void update();
        float getPower(int num);
        float getPercent();
        bool isCharging();
        bool fullCharge();
        std::vector<std::string> battPath;
        float current_watt = 0;
        float current_percent = 0;
        float bat_percent [2][2];
        string current_status="";
        string state [2];

};

extern BatteryStats Battery_Stats;
