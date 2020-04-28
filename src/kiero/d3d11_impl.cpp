#include "kiero.h"

#if KIERO_INCLUDE_D3D11

#include "d3d11_impl.h"
#include <d3d11.h>
#include <assert.h>
#include <iostream>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "../overlay.h"
#include "../notify.h"
#include "font_default.h"
#include "file_utils.h"

struct state {
    ImGuiContext *imgui_ctx = nullptr;
    ImFont* font = nullptr;
    ImFont* font1 = nullptr;
};

static bool cfg_inited = false;
ImVec2 window_size;
overlay_params params {};
static swapchain_stats sw_stats {};
static state state;
static uint32_t vendorID;
static std::string deviceName;
static bool inited = false;

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

typedef long(__stdcall* Present)(IDXGISwapChain*, UINT, UINT);
static Present oPresent = NULL;

long __stdcall hkPresent11(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	static bool init = false;

	if (!init)
	{
		DXGI_SWAP_CHAIN_DESC desc;
		pSwapChain->GetDesc(&desc);

		ID3D11Device* device;
		pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&device);

		ID3D11DeviceContext* context;
		device->GetImmediateContext(&context);

		ImGui::CreateContext();
		ImGui_ImplWin32_Init(desc.OutputWindow);
		ImGui_ImplDX11_Init(device, context);
		imgui_create(context);
		init = true;
	}
	update_hud_info(sw_stats, params, vendorID);
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	position_layer(params, window_size);
	render_imgui(sw_stats, params, window_size, false);

	ImGui::EndFrame();
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	return oPresent(pSwapChain, SyncInterval, Flags);
}

void impl::d3d11::init()
{
	auto ret = kiero::bind(8, (void**)&oPresent, reinterpret_cast<void *>(hkPresent11));
	assert(ret == kiero::Status::Success);
}

#endif // KIERO_INCLUDE_D3D11