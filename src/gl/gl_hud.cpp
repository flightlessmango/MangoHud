#include <cstdlib>
#include <functional>
#include <thread>
#include <string>
#include <iostream>
#include <sstream>
#include <memory>
#include <unistd.h>
#include <spdlog/spdlog.h>
#include <imgui.h>
#ifdef __linux__
#include <implot.h>
#endif
#include "gl_hud.h"
#include "file_utils.h"
#include "notify.h"
#include "blacklist.h"

#include <glad/glad.h>


#define GLX_RENDERER_VENDOR_ID_MESA                      0x8183
#define GLX_RENDERER_DEVICE_ID_MESA                      0x8184

bool glx_mesa_queryInteger(int attrib, unsigned int *value);

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
swapchain_stats sw_stats {};
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

    init_spdlog();
    if (is_blacklisted())
        return;

    parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"), false);
    _params = &params;

   //check for blacklist item in the config file
   for (auto& item : params.blacklist) {
      add_blacklist(item);
   }

    if (sw_stats.engine != EngineTypes::ZINK){
        sw_stats.engine = OPENGL;
        if (lib_loaded("wined3d"))
            sw_stats.engine = WINED3D;
        if (lib_loaded("libtogl.so") || lib_loaded("libtogl_client.so"))
            sw_stats.engine = TOGL;
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
void imgui_create(void *ctx, const gl_wsi plat)
{
    if (inited)
        return;

    if (!ctx)
        return;

    imgui_shutdown();
    imgui_init();
    inited = true;

    if (!gladLoadGL())
        spdlog::error("Failed to initialize OpenGL context, crash incoming");

    deviceName = (char*)glGetString(GL_RENDERER);
    // If we're running zink we want to rely on the vulkan loader for the hud instead.
    if (deviceName.find("zink") != std::string::npos)
        return;

    GetOpenGLVersion(sw_stats.version_gl.major,
        sw_stats.version_gl.minor,
        sw_stats.version_gl.is_gles);

    std::string vendor = (char*)glGetString(GL_VENDOR);
    SPDLOG_DEBUG("vendor: {}, deviceName: {}", vendor, deviceName);
    sw_stats.deviceName = deviceName;
    if (vendor.find("AMD") != std::string::npos
    || deviceName.find("AMD") != std::string::npos
    || deviceName.find("Radeon") != std::string::npos
    || deviceName.find("NAVI") != std::string::npos) {
        vendorID = 0x1002;
    } else if (vendor.find("Intel") != std::string::npos
    || deviceName.find("Intel") != std::string::npos) {
        vendorID = 0x8086;
    } else if (vendor.find("freedreno") != std::string::npos) {
        vendorID = 0x5143;
    }  else {
        vendorID = 0x10de;
    }

    HUDElements.vendorID = vendorID;

    uint32_t device_id = 0;
    if (plat == gl_wsi::GL_WSI_GLX)
        glx_mesa_queryInteger(GLX_RENDERER_DEVICE_ID_MESA, &device_id);

    SPDLOG_DEBUG("GL device id: {:04X}", device_id);
    init_gpu_stats(vendorID, device_id, params);
    sw_stats.gpuName = gpu = remove_parentheses(deviceName);
    SPDLOG_DEBUG("gpu: {}", gpu);
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGuiContext *saved_ctx = ImGui::GetCurrentContext();
    state.imgui_ctx = ImGui::CreateContext();
#ifdef __linux__
    ImPlot::CreateContext();
#endif
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

    create_fonts(nullptr, params, sw_stats.font1, sw_stats.font_text);
    sw_stats.font_params_hash = params.font_params_hash;

    // Restore global context or ours might clash with apps that use Dear ImGui
    ImGui::SetCurrentContext(saved_ctx);
}

void imgui_shutdown()
{
    if (state.imgui_ctx) {
        ImGui::SetCurrentContext(state.imgui_ctx);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext(state.imgui_ctx);
        state.imgui_ctx = nullptr;
    }
    inited = false;
}

void imgui_set_context(void *ctx, const gl_wsi plat)
{
    if (!ctx)
        return;
    imgui_create(ctx, plat);
}

void imgui_render(unsigned int width, unsigned int height)
{
    if (!state.imgui_ctx)
        return;

    static int control_client = -1;
    if (params.control >= 0) {
        control_client_check(params.control, control_client, deviceName);
        process_control_socket(control_client, params);
    }

    check_keybinds(params, vendorID);
    update_hud_info(sw_stats, params, vendorID);

    ImGuiContext *saved_ctx = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(state.imgui_ctx);
    ImGui::GetIO().DisplaySize = ImVec2(width, height);
    if (HUDElements.colors.update)
        HUDElements.convert_colors(params);

    if (sw_stats.font_params_hash != params.font_params_hash)
    {
        sw_stats.font_params_hash = params.font_params_hash;
        create_fonts(nullptr, params, sw_stats.font1, sw_stats.font_text);
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    {
        std::lock_guard<std::mutex> lk(notifier.mutex);
        overlay_new_frame(params);
        position_layer(sw_stats, params, window_size);
        render_imgui(sw_stats, params, window_size, false);
        overlay_end_frame();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::SetCurrentContext(saved_ctx);
}

}} // namespaces
