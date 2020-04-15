#pragma once
#ifndef MANGOHUD_KEYBINDS_H
#define MANGOHUD_KEYBINDS_H

#ifdef HAVE_X11
#include "shared_x11.h"
#include "loaders/loader_x11.h"
#endif

#ifndef KeySym
typedef unsigned long KeySym;
#endif

Clock::time_point last_f2_press, toggle_fps_limit_press , last_f12_press, reload_cfg_press, last_upload_press;

bool keys_are_pressed(const std::vector<KeySym>& keys);

#endif //MANGOHUD_KEYBINDS_H
