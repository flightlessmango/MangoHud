#pragma once
#include "overlay.h"
#include "overlay_params.h"
#include <logging.h>
#include <vector>
#include <unordered_map>
#include <filesystem.h>

class BatteryStats{
    public:
        void numBattery();
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
        int batt_count=0;
        bool batt_check = false;

};

extern BatteryStats Battery_Stats;
