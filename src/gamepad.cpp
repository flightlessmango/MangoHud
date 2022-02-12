#include "gamepad.h"
#include <filesystem.h>
#include <iostream>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace fs = ghc::filesystem;
using namespace std;
std::vector<gamepad> gamepad_data;
std::vector<std::string> list;
bool gamepad_found = false;
int  gamepad_count = 0;
int xbox_count = 0;
int ds4_count = 0;
int ds5_count = 0;
int switch_count = 0;
std::string  xbox_paths [2]{"gip","xpadneo"};

bool operator<(const gamepad& a, const gamepad& b)
{
    return a.name < b.name;
}


void gamepad_update(){
    fs::path path("/sys/class/power_supply");
    list.clear();
    xbox_count = 0;
    ds4_count = 0;
    ds5_count = 0;
    switch_count = 0;
    for (auto &p : fs::directory_iterator(path)) {
        string fileName = p.path().filename();
        //CHECK XONE AND XPADNEO DEVICES
        for (string n : xbox_paths ) {
            if (fileName.find(n) != std::string::npos) {
                list.push_back(p.path());
                gamepad_found = true;
                xbox_count += 1;
            }
        }
        //CHECK FOR DUAL SHOCK 4 DEVICES
        if (fileName.find("sony_controller") != std::string::npos) {
            list.push_back(p.path());
            gamepad_found = true;
            ds4_count +=1 ;
        }
        if (fileName.find("ps-controller") != std::string::npos) {
            list.push_back(p.path());
            gamepad_found = true;
            ds5_count +=1 ;
        }
        //CHECK FOR NINTENDO SWITCH DEVICES
        if (fileName.find("nintendo_switch_controller") != std::string::npos) {
            list.push_back(p.path());
            gamepad_found = true;
            switch_count += 1;
        }
    }
}


void gamepad_info () {
    gamepad_count = 0;
    gamepad_data.clear();
    int xbox_counter = 0;
    int ds4_counter = 0;
    int ds5_counter = 0;
    int switch_counter = 0;

    for (auto &path : list ) {
        //Set devices paths
        std::string capacity = path + "/capacity";
        std::string capacity_level = path + "/capacity_level";
        std::string status = path + "/status";
        std::ifstream input_capacity(capacity);
        std::ifstream input_capacity_level(capacity_level);
        std::ifstream input_status(status);
        std::string line;

        gamepad_data.push_back(gamepad());

        //Xone devices
        if (path.find("gip") != std::string::npos || path.find("xpadneo") != std::string::npos) {
            if (xbox_count == 1 )
                gamepad_data[gamepad_count].name = "XBOX PAD";
            else
                gamepad_data[gamepad_count].name = "XBOX PAD-" + to_string(xbox_counter + 1);
            xbox_counter++;
        }
        //DualShock 4 devices
        if (path.find("sony_controller") != std::string::npos) {
            if (ds4_count == 1)
                gamepad_data[gamepad_count].name = "DS4 PAD";
            else
                gamepad_data[gamepad_count].name = "DS4 PAD-" + to_string(ds4_counter + 1);
            ds4_counter++;
        }
        //DualSense 5 devices
        if (path.find("ps-controller") != std::string::npos) {
            if (ds5_count == 1)
                gamepad_data[gamepad_count].name = "DS5 PAD";
            else
                gamepad_data[gamepad_count].name = "DS5 PAD-" + to_string(ds5_counter + 1);
            ds5_counter++;
        }
        //Nintendo Switch devices
        if (path.find("nintendo_switch_controller") != std::string::npos) {
            if (switch_count == 1)
                gamepad_data[gamepad_count].name = "SWITCH PAD";
            else
                gamepad_data[gamepad_count].name = "SWITCH PAD-" + to_string(switch_counter + 1);
            switch_counter++;
        }
        //Get device status
        if (std::getline(input_status, line))
               gamepad_data[gamepad_count].state = line;

        //Get device Battery
        if (fs::exists(capacity)) {
            if (std::getline(input_capacity, line)) {
                switch(std::stoi(line)) {
                    case 0 ... 25:
                        gamepad_data[gamepad_count].battery = "Low";
                        break;
                    case 26 ... 49:
                        gamepad_data[gamepad_count].battery = "Normal";
                        break;
                    case 50 ... 74:
                        gamepad_data[gamepad_count].battery = "High";
                        break;
                    case 75 ... 100:
                        gamepad_data[gamepad_count].battery = "Full";
                        break;
                }
            }
        }
        else {
            if (std::getline(input_capacity_level, line)) {
                gamepad_data[gamepad_count].battery = line;
            }
        }
        std::sort(gamepad_data.begin(), gamepad_data.end());
        gamepad_count += 1;

    }
}
