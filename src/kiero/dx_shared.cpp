#include "dx_shared.h"
#include <dxgi.h>

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

void imgui_create(void *ctx, void *device)
{
    if (inited)
        return;
    inited = true;

    imgui_init();

    // DX10+
    if (device) {
        IUnknown* pUnknown = reinterpret_cast<IUnknown*>(device);
        IDXGIDevice* pDXGIDevice;
        HRESULT hr = pUnknown->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);
        if (S_OK == hr) {
            IDXGIAdapter* pDXGIAdapter;
            pDXGIDevice->GetAdapter(&pDXGIAdapter);
            DXGI_ADAPTER_DESC adapterDesc;
            hr = pDXGIAdapter->GetDesc(&adapterDesc);

            if (S_OK == hr) {
                vendorID = adapterDesc.VendorId;
                char buf[256]{};
                wcstombs_s(nullptr, buf, adapterDesc.Description, sizeof(adapterDesc.Description));
                deviceName = buf;
            }
        }
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