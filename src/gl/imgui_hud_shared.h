#pragma once
#include <imgui.h>
#include "overlay.h"
#include "notify.h"

namespace MangoHud { namespace GL {

extern notify_thread notifier;
extern ImVec2 window_size;
extern bool inited;
extern overlay_params params;

void imgui_init();

}} // namespaces
