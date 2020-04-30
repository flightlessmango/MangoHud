#include "../overlay.h"
#include "imgui.h"
#include "../notify.h"
#include "font_default.h"
#include "file_utils.h"

struct state {
    ImGuiContext *imgui_ctx = nullptr;
    ImFont* font = nullptr;
    ImFont* font1 = nullptr;
};

extern bool cfg_inited;
extern ImVec2 window_size;
extern struct overlay_params params;
extern struct swapchain_stats sw_stats;
extern struct state state;
extern uint32_t vendorID;
extern std::string deviceName;
extern bool inited;

void imgui_create(void *ctx);
void imgui_init(void);