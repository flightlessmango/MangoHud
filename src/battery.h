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
        void update();
        float getPower();
        float getPercent();
        bool isCharging();
        bool fullCharge();
        string battPath[2];
        float current_watt = 0;
        float current_percent = 0;
        string current_status="";
        string state [2];

};

extern BatteryStats Battery_Stats;
