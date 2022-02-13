#pragma once
#ifndef MANGOHUD_GAMEPAD_H
#define MANGOHUD_GAMEPAD_H
#include <vector>
#include <string>

struct gamepad {
    std::string battery;
    std::string name;
    bool report_percent;
    std::string battery_percent;
    bool is_charging;
};

extern std::vector<gamepad> gamepad_data;

extern bool gamepad_found;
extern int gamepad_count;
void gamepad_update();
void gamepad_info();


#endif // MANGOHUD_GAMEPAD_H
