#include "device.h"
#include <filesystem.h>
#include <iostream>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace fs = ghc::filesystem;
using namespace std;
std::mutex device_lock;
std::vector<device_batt> device_data;
std::vector<std::string> list;
bool device_found = false;
bool check_gamepad = false;
bool check_mouse = false;
int  device_count = 0;
int xbox_count = 0;
int ds4_count = 0;
int ds5_count = 0;
int switch_count = 0;
int bitdo_count = 0;
int logi_count = 0; //Logitech devices, mice & keyboards etc.
int shield_count = 0;

std::string  xbox_paths [2]{"gip","xpadneo"};

static bool operator<(const device_batt& a, const device_batt& b)
{
    return a.name < b.name;
}


void device_update(const struct overlay_params& params){
    std::unique_lock<std::mutex> l(device_lock);
    fs::path path("/sys/class/power_supply");
    list.clear();
    xbox_count = 0;
    ds4_count = 0;
    ds5_count = 0;
    switch_count = 0;
    bitdo_count = 0;
    shield_count = 0;
    for (auto &p : fs::directory_iterator(path)) {
        string fileName = p.path().filename();
//Gamepads
        if (std::find(params.device_battery.begin(), params.device_battery.end(), "gamepad") != params.device_battery.end()){
            check_gamepad = true;
            //CHECK XONE AND XPADNEO DEVICES
            for (string n : xbox_paths ) {
                if (fileName.find(n) != std::string::npos) {
                    list.push_back(p.path());
                    device_found = true;
                    xbox_count += 1;
                }
            }
            //CHECK FOR DUAL SHOCK 4 DEVICES
            if (fileName.find("sony_controller") != std::string::npos) {
                list.push_back(p.path());
                device_found = true;
                ds4_count +=1 ;
            }
            if (fileName.find("ps-controller") != std::string::npos) {
                list.push_back(p.path());
                device_found = true;
                ds5_count +=1 ;
            }
            //CHECK FOR NINTENDO SWITCH DEVICES
            if (fileName.find("nintendo_switch_controller") != std::string::npos) {
                list.push_back(p.path());
                device_found = true;
                switch_count += 1;
            }
            //CHECK * BITDO DEVICES
            if (fileName.find("hid-e4") != std::string::npos) {
                list.push_back(p.path());
                device_found = true;
                bitdo_count += 1;
            }
            //CHECK NVIDIA SHIELD DEVICES
            if (fileName.find("thunderstrike") != std::string::npos) {
                list.push_back(p.path());
                device_found = true;
                shield_count += 1;
            }
        }

// Mice and Keyboards
        //CHECK LOGITECH DEVICES
         if (std::find(params.device_battery.begin(), params.device_battery.end(), "mouse") != params.device_battery.end()) {
            check_mouse = true;
            if (fileName.find("hidpp_battery") != std::string::npos) {
                list.push_back(p.path());
                device_found = true;
            }
         }
    }
}


void device_info () {
    std::unique_lock<std::mutex> l(device_lock);
    device_count = 0;
    device_data.clear();
    //gamepad counters
    int xbox_counter = 0;
    int ds4_counter = 0;
    int ds5_counter = 0;
    int switch_counter = 0;
    int bitdo_counter = 0;
    int shield_counter = 0;

    for (auto &path : list ) {
        //Set devices paths
        std::string capacity = path + "/capacity";
        std::string capacity_level = path + "/capacity_level";
        std::string status = path + "/status";
        std::string model = path + "/model_name";
        std::ifstream input_capacity(capacity);
        std::ifstream input_capacity_level(capacity_level);
        std::ifstream input_status(status);
        std::ifstream device_name(model);
        std::string line;

        device_data.push_back(device_batt());

// GAMEPADS
        //Xone and xpadneo devices
        if (check_gamepad == true) {
            if (path.find("gip") != std::string::npos || path.find("xpadneo") != std::string::npos) {
                if (xbox_count == 1 )
                    device_data[device_count].name = "XBOX PAD";
                else
                    device_data[device_count].name = "XBOX PAD-" + to_string(xbox_counter + 1);
                xbox_counter++;
            }
            //DualShock 4 devices
            if (path.find("sony_controller") != std::string::npos) {
                if (ds4_count == 1)
                    device_data[device_count].name = "DS4 PAD";
                else
                    device_data[device_count].name = "DS4 PAD-" + to_string(ds4_counter + 1);
                ds4_counter++;
            }
            //DualSense 5 devices
            //Dual Shock 4 added to hid-playstation in Linux 6.2
            if (path.find("ps-controller") != std::string::npos) {
                if (ds5_count == 1)
                    device_data[device_count].name = "DS4/5 PAD";
                else
                    device_data[device_count].name = "DS4/5 PAD-" + to_string(ds5_counter + 1);
                ds5_counter++;
            }
            //Nintendo Switch devices
            if (path.find("nintendo_switch_controller") != std::string::npos) {
                if (switch_count == 1)
                    device_data[device_count].name = "SWITCH PAD";
                else
                    device_data[device_count].name = "SWITCH PAD-" + to_string(switch_counter + 1);
                switch_counter++;
            }
            //8bitdo devices
            if (path.find("hid-e4") != std::string::npos) {
                if (bitdo_count == 1)
                    device_data[device_count].name = "8BITDO PAD";
                else
                    device_data[device_count].name = "8BITDO PAD-" + to_string(bitdo_counter + 1);
                bitdo_counter++;
            }
            //Shield devices
            if (path.find("thunderstrike") != std::string::npos) {
                if (shield_count == 1)
                    device_data[device_count].name = "SHIELD PAD";
                else
                    device_data[device_count].name = "SHIELD PAD-" + to_string(shield_counter + 1);
                shield_counter++;
            }
        }

// MICE AND KEYBOARDS
        //Logitech Devices
         if (check_mouse == true) {
            if (path.find("hidpp_battery") != std::string::npos) {
                // Find a good way truncate name or retreive device type before using this
                    // if (std::getline(device_name, line)) {
                    //     device_data[device_count].name = line;
                    // }
                device_data[device_count].name = "LOGI MOUSE/KB";
            }
         }

        //Get device charging status
        if (std::getline(input_status, line)) {
            if (line == "Charging" || line == "Full")
                 device_data[device_count].is_charging = true;
        }
        //Get device Battery
        if (fs::exists(capacity)) {
            if (std::getline(input_capacity, line)) {
                device_data[device_count].battery_percent = line;
                device_data[device_count].report_percent = true;
                switch(std::stoi(line)) {
                    case 0 ... 25:
                        device_data[device_count].battery = "Low";
                        break;
                    case 26 ... 49:
                        device_data[device_count].battery = "Normal";
                        break;
                    case 50 ... 74:
                        device_data[device_count].battery = "High";
                        break;
                    case 75 ... 100:
                        device_data[device_count].battery = "Full";
                        break;
                }
            }
        }
        else {
            if (std::getline(input_capacity_level, line)) {
                device_data[device_count].battery = line;
            }
        }
        std::sort(device_data.begin(), device_data.end());
        device_count += 1;

    }
}
