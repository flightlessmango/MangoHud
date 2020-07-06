#pragma once
#ifndef MANGOHUD_GL_IMGUI_HUD_H
#define MANGOHUD_GL_IMGUI_HUD_H

#include "overlay.h"
#include "imgui_impl_opengl3.h"

namespace MangoHud { namespace GL {

extern overlay_params params;
void imgui_init();
void imgui_create(void *ctx);
void imgui_shutdown();
void imgui_set_context(void *ctx);
void imgui_render(unsigned int width, unsigned int height);

}} // namespace

#endif //MANGOHUD_GL_IMGUI_HUD_H