#pragma once
#ifndef MANGOHUD_GL_IMGUI_HUD_H
#define MANGOHUD_GL_IMGUI_HUD_H

#include "overlay.h"
#include "imgui_impl_opengl3.h"

namespace MangoHud { namespace GL {

enum gl_platform
{
    GLX,
    EGL,
};

extern overlay_params params;
void imgui_init();
void imgui_create(void* ctx, const gl_platform plat);
void imgui_shutdown();
void imgui_render(unsigned int width, unsigned int height);

}} // namespace

#endif //MANGOHUD_GL_IMGUI_HUD_H
