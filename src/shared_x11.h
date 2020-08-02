#pragma once
#ifndef MANGOHUD_SHARED_X11_H
#define MANGOHUD_SHARED_X11_H

#include <X11/Xlib.h>

Display* get_xdisplay();
bool init_x11();

#endif //MANGOHUD_SHARED_X11_H
