#include <spdlog/spdlog.h>
#include <filesystem.h>
#include "battery.h"

namespace fs = ghc::filesystem;
using namespace std;

void BatteryStats::numBattery() {
    int batteryCount = 0;
    if (!fs::exists("/sys/class/power_supply/")) {
         batteryCount = 0;
    }
    fs::path path("/sys/class/power_supply/");
    for (auto& p : fs::directory_iterator(path)) {
        string fileName = p.path().filename();
        if (fileName.find("BAT") != std::string::npos) {
            battPath[batteryCount] = p.path();
            batteryCount += 1;
        }
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
    float charge_n = 0;
    float charge_f = 0;
    for(int i = 0; i < batt_count; i++) {
        string syspath = battPath[i];
        string charge_now = syspath + "/charge_now";
        string charge_full = syspath + "/charge_full";
        string energy_now = syspath + "/energy_now";
        string energy_full = syspath + "/energy_full";
        string capacity = syspath + "/capacity";

        if (fs::exists(charge_now)) {
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

        else if (fs::exists(energy_now)) {
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

float BatteryStats::getPower() {
    float current = 0;
    float voltage = 0;
    for(int i = 0; i < batt_count; i++) {
        string syspath = battPath[i];
        string current_power = syspath + "/current_now";
        string current_voltage = syspath + "/voltage_now";
        string power_now = syspath + "/power_now";
        string status = syspath + "/status";

        std::ifstream input(status);
        std::string line;
        if(std::getline(input,line)) {
            current_status= line;
            state[i]=current_status;
        }

        if (state[i] == "Charging" ||  state[i] == "Unknown") {
            return 0;
        }

        if (fs::exists(current_power)) {
            std::ifstream input(current_power);
            std::string line;
            if(std::getline(input,line)) {
                current += (stof(line) / 1000000);
            }
            std::ifstream input2(current_voltage);
            if(std::getline(input2, line)) {
                voltage += (stof(line) / 1000000);
            }
        }
        else {
            std::ifstream input(power_now);
            std::string line;
            if(std::getline(input,line)) {
                current += (stof(line) / 1000000);
                voltage = 1;
            }
        }
    }
    return current * voltage;
}

float BatteryStats::getTimeRemaining(){
    float current = 0;
    float charge = 0;
    for(int i = 0; i < batt_count; i++) {
        string syspath = battPath[i];
        string current_now = syspath + "/current_now";
        string charge_now = syspath + "/charge_now";
        string voltage_now = syspath + "/voltage_now";
        string power_now = syspath + "/power_now";

        if (fs::exists(current_now)) {
            std::ifstream input(current_now);
            std::string line;
            if(std::getline(input, line)){
                current_now_vec.push_back(stof(line));
            }
        } else if (fs::exists(power_now)){
            float voltage = 0;
            float power = 0;
            std::ifstream input_power(power_now);
            std::ifstream input_voltage(power_now);
            std::string line;
            if(std::getline(input_voltage, line)){
                voltage = stof(line);
            }
            if(std::getline(input_power, line)){
                power = stof(line);
            }
            current_now_vec.push_back(power / voltage);
        }
        if (fs::exists(charge_now)){
            std::string line;
            std::ifstream input(charge_now);
            if(std::getline(input, line)){
                charge += stof(line);
            }
        }
        if (current_now_vec.size() > 25)
            current_now_vec.erase(current_now_vec.begin());
    }
    
    for(auto& current_now : current_now_vec){
        current += current_now;
    }
    current /= current_now_vec.size();

    return charge / current;
}

BatteryStats Battery_Stats;
