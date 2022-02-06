#pragma once
#ifndef MANGOHUD_CONFIG_H
#define MANGOHUD_CONFIG_H

#include "overlay_params.h"
#include <string>

bool parseConfigFile(overlay_params& p);
std::string get_program_name();

#endif //MANGOHUD_CONFIG_H
