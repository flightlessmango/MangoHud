#include "battery.h"

void BatteryStats::findFiles(){
    FILE *file;
    file = fopen("/sys/class/power_supply/BAT1/current_now", "r");
    powerMap["current_now"] = {file, 0};
    file = fopen("/sys/class/power_supply/BAT1/voltage_now", "r");
    powerMap["voltage_now"] = {file, 0};
    // file = fopen("/sys/class/power_supply/BAT1/charge_now", "r");
    // powerMap["charge_now"] = {file, 0};
    // file = fopen("/sys/class/power_supply/BAT1/charge_full", "r");
    // powerMap["charge_full"] = {file, 0};
    
    files_fetched = true;
}

void BatteryStats::update(){
    if (!files_fetched)
        findFiles();

    for(auto &pair : powerMap){
        if(pair.second.file) {
            rewind(pair.second.file);
            fflush(pair.second.file);
            fscanf(pair.second.file, "%f", &pair.second.value);
            pair.second.value /= 1000000;
        }
    }
    current_watt = powerMap["current_now"].value * powerMap["voltage_now"].value;
}

BatteryStats Battery_Stats;