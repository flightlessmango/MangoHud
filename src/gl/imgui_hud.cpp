#include <cstdlib>
#include <functional>
#include <thread>
#include <string>
#include <iostream>
#include <memory>
#include <unistd.h>
#include <imgui.h>
#include "font_default.h"
#include "cpu.h"
#include "file_utils.h"
#include "imgui_hud.h"
#include "notify.h"
#include "blacklist.h"
#include "overlay.h"

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

   //check for blacklist item in the config file
   for (auto& item : params.blacklist) {
      add_blacklist(item);
   }

    if (engine != EngineTypes::ZINK){
        auto pid = getpid();
        string find_wined3d = "lsof -w -lnPX -L -p " + to_string(pid) + " | grep -oh wined3d";
        string ret_wined3d = exec(find_wined3d);
        if (ret_wined3d == "wined3d\n" )
            engine = WINED3D;
        else
            engine = OpenGL;
    }
    is_blacklisted(true);
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

    if (!ctx)
        return;

    imgui_shutdown();
    imgui_init();
    inited = true;

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
    get_device_name(vendorID, deviceID, sw_stats);
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
    HUDElements.convert_colors(false, params);

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

    create_fonts(params, sw_stats.font1, sw_stats.font_text);
    sw_stats.font_params_hash = params.font_params_hash;

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
    if (!ctx)
        return;

#ifndef NDEBUG
    std::cerr << __func__ << ": " << ctx << std::endl;
#endif
    imgui_create(ctx);
}

void imgui_render(unsigned int width, unsigned int height)
{
    if (!state.imgui_ctx)
        return;

    check_keybinds(sw_stats, params, vendorID);
    update_hud_info(sw_stats, params, vendorID);

    ImGuiContext *saved_ctx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(state.imgui_ctx);
    ImGui::GetIO().DisplaySize = ImVec2(width, height);
    if (HUDElements.colors.update)
        HUDElements.convert_colors(params);

    if (sw_stats.font_params_hash != params.font_params_hash)
    {
        sw_stats.font_params_hash = params.font_params_hash;
        create_fonts(params, sw_stats.font1, sw_stats.font_text);
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    {
        std::lock_guard<std::mutex> lk(notifier.mutex);
        position_layer(sw_stats, params, window_size);
        render_imgui(sw_stats, params, window_size, false);
    }
    ImGui::PopStyleVar(3);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::SetCurrentContext(saved_ctx);
}

}} // namespaces
