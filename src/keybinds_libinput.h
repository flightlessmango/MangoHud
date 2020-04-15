#pragma once
#include "overlay_params.h"

bool libinput_keys_are_pressed(const std::vector<KeySym>& x11_keybinds);
void start_input(const overlay_params& params);
