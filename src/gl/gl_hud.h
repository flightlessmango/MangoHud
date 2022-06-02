#pragma once
#ifndef MANGOHUD_GL_IMGUI_HUD_H
#define MANGOHUD_GL_IMGUI_HUD_H

#include "overlay.h"
#include "gl_renderer.h"

namespace MangoHud { namespace GL {

extern wsi_connection wsi_conn;

enum GL_SESSION
{
    GL_SESSION_UNKNOWN,
    GL_SESSION_X11,
    GL_SESSION_WL,
};

extern overlay_params params;
void imgui_init();
void imgui_create(void *ctx, GL_SESSION);
void imgui_shutdown();
void imgui_set_context(void *ctx);
void imgui_render(unsigned int width, unsigned int height);

}} // namespace

#endif //MANGOHUD_GL_IMGUI_HUD_H
