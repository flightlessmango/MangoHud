#pragma once

void imgui_init();
void imgui_create(void *ctx);
void imgui_shutdown();
void imgui_set_context(void *ctx);
void imgui_render(unsigned int width, unsigned int height);
