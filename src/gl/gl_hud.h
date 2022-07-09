#pragma once
#ifndef MANGOHUD_GL_IMGUI_HUD_H
#define MANGOHUD_GL_IMGUI_HUD_H

#include "overlay.h"
#include "gl_renderer.h"

namespace MangoHud { namespace GL {

enum gl_wsi
{
    GL_WSI_UNKNOWN,
    GL_WSI_GLX,
    GL_WSI_EGL,
};

extern overlay_params params;
void imgui_init();
void imgui_create(void *ctx, const gl_wsi plat);
void imgui_shutdown();
void imgui_set_context(void *ctx, const gl_wsi plat);
void imgui_render(unsigned int width, unsigned int height);

}} // namespace

#endif //MANGOHUD_GL_IMGUI_HUD_H
