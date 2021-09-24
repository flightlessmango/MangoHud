#pragma once
#include <libdrm/drm.h>

bool authenticate_drm_xcb(drm_magic_t magic);
bool authenticate_drm(int fd);
