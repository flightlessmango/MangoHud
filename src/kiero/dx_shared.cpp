#include "dx_shared.h"

bool cfg_inited = false;
ImVec2 window_size;
overlay_params params {};
struct swapchain_stats sw_stats {};
struct state state;
uint32_t vendorID;
std::string deviceName;
bool inited = false;

void imgui_init()
{
    if (cfg_inited)
        return;
    parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"));
    window_size = ImVec2(params.width, params.height);
    cfg_inited = true;
    init_cpu_stats(params);
}

void imgui_create(void *ctx)
{
    if (inited)
        return;
    inited = true;

    imgui_init();
    deviceName = "something";
    if (deviceName.find("Radeon") != std::string::npos
    || deviceName.find("AMD") != std::string::npos){
        vendorID = 0x1002;
    } else {
        vendorID = 0x10de;
    }
    init_gpu_stats(vendorID, params);
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGuiContext *saved_ctx = ImGui::GetCurrentContext();
    state.imgui_ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    imgui_custom_style(params);
    ImGui::GetIO().IniFilename = NULL;
	int font_size = params.font_size;
    if (!font_size)
        font_size = 24;

    if (!params.font_file.empty() && file_exists(params.font_file)) {
        state.font = io.Fonts->AddFontFromFileTTF(params.font_file.c_str(), font_size);
        state.font1 = io.Fonts->AddFontFromFileTTF(params.font_file.c_str(), font_size * 0.55f);
    } else {
        ImFontConfig font_cfg = ImFontConfig();
        const char* ttf_compressed_base85 = GetDefaultCompressedFontDataTTFBase85();
        const ImWchar* glyph_ranges = io.Fonts->GetGlyphRangesDefault();

        state.font = io.Fonts->AddFontFromMemoryCompressedBase85TTF(ttf_compressed_base85, font_size, &font_cfg, glyph_ranges);
        state.font1 = io.Fonts->AddFontFromMemoryCompressedBase85TTF(ttf_compressed_base85, font_size * 0.55, &font_cfg, glyph_ranges);
    }
    sw_stats.font1 = state.font1;

    // Restore global context or ours might clash with apps that use Dear ImGui
    ImGui::SetCurrentContext(saved_ctx);
}