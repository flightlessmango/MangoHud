#include <stdio.h>
#include "battery.h"
#include <filesystem.h>

namespace fs = ghc::filesystem;

int BatteryStats::numBattery() {
    int batteryCount = 0;
    if (!fs::exists("/sys/class/power_supply/")) {
        return batteryCount = 0;
    }
    fs::path path("/sys/class/power_supply/");
    for (auto& p : fs::directory_iterator(path)) {
        string fileName = p.path().filename();
        if (fileName.find("BAT") != std::string::npos) {
            batteryCount += 1;
            battPath.push_back(p.path());
        }
    }
    return batteryCount;
}


void BatteryStats::update() {
    if (numBattery() > 0) {
        if (numBattery() == 1) {
            current_watt = getPower(0);
            current_percent = getPercent(0) * 100;
        }

        else if (numBattery() == 2) {
            float bat1_power = getPower(0);
            float bat2_power = getPower(1);
            getPercent(0);
            getPercent(1);
            //bat_percent[][] = [bat1 or 2] [power now or power full]

            float energy=bat_percent[0][0] + bat_percent[1][0];
            float energy_full=bat_percent[0][1] + bat_percent[1][1];
            current_watt = (bat1_power + bat2_power);
            current_percent = (energy / energy_full) * 100;
        }
    }
}

float BatteryStats::getPercent(int num)
{
    string syspath = battPath[num];
    string charge_now = syspath + "/charge_now";
    string charge_full = syspath + "/charge_full";
    string energy_now = syspath + "/energy_now";
    string energy_full = syspath + "/energy_full";
    string capacity = syspath + "/capacity";

    if (fs::exists(charge_now)) {
        float charge_n = 0;
        float charge_f = 0;

        std::ifstream input(charge_now);
        std::string line;
        if(std::getline(input, line)) {
            charge_n = (stof(line) / 1000000);
            bat_percent[num][0]=charge_n;
        }

        std::ifstream input2(charge_full);
        if(std::getline(input2, line)) {
            charge_f = (stof(line) / 1000000);
            bat_percent[num][1]=charge_f;
        }

        return (charge_n / charge_f);
    }

    else if (fs::exists(energy_now)) {
        float energy_n = 0;
        float energy_f = 0;

        std::ifstream input(energy_now);
        std::string line;
        if(std::getline(input, line)) {
            energy_n = (stof(line) / 1000000);
            bat_percent[num][0]=energy_n;
        }

        std::ifstream input2(energy_full);
        if(std::getline(input2, line)) {
            energy_f = (stof(line) / 1000000);
            bat_percent[num][1]=energy_f;
        }

        return (energy_n / energy_f);

    }

    else {
        float percent=0;
        // using /sys/class/power_supply/BAT*/capacity
        // No way to get an accurate reading just average the percents if mutiple batteries
        std::ifstream input(capacity);
        std::string line;
        if(std::getline(input, line)) {
            percent = stof(line) / 100;
            bat_percent[num][0]=percent;
            bat_percent[num][1]=1.0;
        }

        return percent;
    }
}

float BatteryStats::getPower(int batt_num) {
    string syspath = battPath[batt_num];
    string current_power = syspath + "/current_now";
    string current_voltage = syspath + "/voltage_now";
    string power_now = syspath + "/power_now";

    if (isCharging()) {
        return 0;
    }

    else if (fs::exists(current_power)) {
        float current = 0;
        float voltage = 0;

        std::ifstream input(current_power);
        std::string line;

        if(std::getline(input,line)) {
            current = (stof(line) / 1000000);
        }
        std::ifstream input2(current_voltage);
        if(std::getline(input2, line)) {
            voltage = (stof(line) / 1000000);
        }

        return current * voltage;
    }
    else  {
        float power = 0;
        std::ifstream input(power_now);
        std::string line;
        if(std::getline(input,line)) {
            power = (stof(line) / 1000000);
        }

        return power;

    }

}

bool BatteryStats::isCharging() {
    if (numBattery() > 0) {
        for(int i =0; i < 2; i++) {
            string syspath = battPath[i];
            string status = syspath + "/status";
            std::ifstream input(status);
            std::string line;

            if(std::getline(input,line)) {
                current_status= line;
                state[i]=current_status;
            }
        }
        for(int i =0; i < 2; i++) {
            if (state[i] == "Charging") {
            return true;
            }
        }
    }

    return false;
}


bool BatteryStats::fullCharge(){
    //check if both batteries are fully charged
    int charged =0;
    for(int i =0; i < 2; i++) {
        if (state[i] == "Full") {
            charged +=1;
        }
    }
    if (charged == 2) {
        return true;
    }

    else {
        return false;
    }
}

BatteryStats Battery_Stats;
