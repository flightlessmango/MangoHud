#include <cstdlib>
#include <functional>
#include <thread>
#include <string>
#include <iostream>
#include <memory>
#include <imgui.h>
#include "font_default.h"
#include "cpu.h"
#include "file_utils.h"
#include "imgui_hud.h"
#include "notify.h"

#ifdef HAVE_DBUS
#include "dbus_info.h"
#endif

#include <glad/glad.h>

namespace MangoHud { namespace GL {

struct GLVec
{
    GLint v[4];

    GLint operator[] (size_t i)
    {
        return v[i];
    }

    bool operator== (const GLVec& r)
    {
        return v[0] == r.v[0]
            && v[1] == r.v[1]
            && v[2] == r.v[2]
            && v[3] == r.v[3];
    }

    bool operator!= (const GLVec& r)
    {
        return !(*this == r);
    }
};

struct state {
    ImGuiContext *imgui_ctx = nullptr;
    ImFont* font = nullptr;
    ImFont* font1 = nullptr;
};

static GLVec last_vp {}, last_sb {};
static swapchain_stats sw_stats {};
static state state;
static uint32_t vendorID;
static std::string deviceName;

static notify_thread notifier;
static bool cfg_inited = false;
static ImVec2 window_size;
static bool inited = false;
overlay_params params {};

// seems to quit by itself though
static std::unique_ptr<notify_thread, std::function<void(notify_thread *)>>
    stop_it(&notifier, [](notify_thread *n){ stop_notifier(*n); });

void imgui_init()
{
    if (cfg_inited)
        return;
    parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"));
    notifier.params = &params;
    start_notifier(notifier);
    window_size = ImVec2(params.width, params.height);
    init_system_info();
    cfg_inited = true;
    init_cpu_stats(params);
}

//static
void imgui_create(void *ctx)
{
    if (inited)
        return;
    inited = true;

    if (!ctx)
        return;

    imgui_init();

    gladLoadGL();

    GetOpenGLVersion(sw_stats.version_gl.major,
        sw_stats.version_gl.minor,
        sw_stats.version_gl.is_gles);

    deviceName = (char*)glGetString(GL_RENDERER);
    sw_stats.deviceName = deviceName;
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
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();
    imgui_custom_style(params);

    glGetIntegerv (GL_VIEWPORT, last_vp.v);
    glGetIntegerv (GL_SCISSOR_BOX, last_sb.v);

    ImGui::GetIO().IniFilename = NULL;
    ImGui::GetIO().DisplaySize = ImVec2(last_vp[2], last_vp[3]);

    ImGui_ImplOpenGL3_Init();
    // Make a dummy GL call (we don't actually need the result)
    // IF YOU GET A CRASH HERE: it probably means that you haven't initialized the OpenGL function loader used by this code.
    // Desktop OpenGL 3/4 need a function loader. See the IMGUI_IMPL_OPENGL_LOADER_xxx explanation above.
    GLint current_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &current_texture);

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

void imgui_shutdown()
{
#ifndef NDEBUG
    std::cerr << __func__ << std::endl;
#endif

    if (state.imgui_ctx) {
        ImGui::SetCurrentContext(state.imgui_ctx);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext(state.imgui_ctx);
        state.imgui_ctx = nullptr;
    }
    inited = false;
}

void imgui_set_context(void *ctx)
{
    if (!ctx) {
        imgui_shutdown();
        return;
    }
#ifndef NDEBUG
    std::cerr << __func__ << ": " << ctx << std::endl;
#endif
    imgui_create(ctx);
}

void imgui_render(unsigned int width, unsigned int height)
{
    if (!state.imgui_ctx)
        return;

    check_keybinds(params);
    update_hud_info(sw_stats, params, vendorID);

    ImGuiContext *saved_ctx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(state.imgui_ctx);
    ImGui::GetIO().DisplaySize = ImVec2(width, height);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    {
        std::lock_guard<std::mutex> lk(notifier.mutex);
        position_layer(params, window_size);
        render_imgui(sw_stats, params, window_size, "OpenGL");
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::SetCurrentContext(saved_ctx);
}

}} // namespaces
