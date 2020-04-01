#pragma once

#include "imgui_impl_opengl3.h"

namespace MangoHud { namespace GL {

void VARIANT(imgui_init)();
void VARIANT(imgui_create)(void *ctx);
void VARIANT(imgui_shutdown)();
void VARIANT(imgui_set_context)(void *ctx);
void VARIANT(imgui_render)(unsigned int width, unsigned int height);

}} // namespace
