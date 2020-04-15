#pragma once
#include "overlay_params.h"

bool libinput_key_is_pressed(std::vector<KeySym> x11_keybinds);
void start_input(const overlay_params& params);
