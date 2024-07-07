// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#include <sys/ipc.h>
#include <sys/msg.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <thread>
#include <unistd.h>
#include "../overlay.h"
#include "../notify.h"
#include "mangoapp.h"
#include "mangoapp_proto.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "../amdgpu.h"
#ifdef __linux__
#include "implot.h"
#endif

#define GLFW_EXPOSE_NATIVE_EGL
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>

#define namespace _namespace
#include <wlr-layer-shell-unstable-v1-client-protocol.h>
#undef namespace

#include "nlohmann/json.hpp"
using json = nlohmann::json;
using namespace std;

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

swapchain_stats sw_stats {};
overlay_params params {};
static ImVec2 window_size;
static uint32_t vendorID;
static std::string deviceName;
static notify_thread notifier;
static int msgid;
static bool mangoapp_paused = false;
std::mutex mangoapp_m;
std::condition_variable mangoapp_cv;
static uint8_t raw_msg[1024] = {0};
static uint32_t screenWidth = 1280, screenHeight = 800;
struct zwlr_layer_surface_v1 *layer_surface;

static void ctrl_thread(){
    while (1){
        const struct mangoapp_ctrl_msgid1_v1 *mangoapp_ctrl_v1 = (const struct mangoapp_ctrl_msgid1_v1*) raw_msg;
        memset(raw_msg, 0, sizeof(raw_msg));
        msgrcv(msgid, (void *) raw_msg, sizeof(raw_msg), 2, 0);
        switch (mangoapp_ctrl_v1->log_session) {
            case 0:
                // Keep as-is
                break;
            case 1:
                if (!logger->is_active())
                    logger->start_logging();
                break;
            case 2:
                if (logger->is_active())
                    logger->stop_logging();
                break;
            case 3:
                logger->is_active() ? logger->stop_logging() : logger->start_logging();
                break;
        }
        switch (mangoapp_ctrl_v1->reload_config) {
            case 0:
                break;
            case 1:
                parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"), false);
                break;
            case 2:
                break;
            case 3:
                parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"), false);
                break;
        }
        {
            std::lock_guard<std::mutex> lk(mangoapp_m);
            switch (mangoapp_ctrl_v1->no_display){
                case 0:
                    // Keep as-is
                    break;
                case 1:
                    params.no_display = 1;
                    break;
                case 2:
                    params.no_display = 0;
                    break;
                case 3:
                    params.no_display ? params.no_display = 0 : params.no_display = 1;
                    break;
            }
        }
        mangoapp_cv.notify_one();
    }
}

bool new_frame = false;

static void gamescope_frametime(uint64_t app_frametime_ns, uint64_t latency_ns){
    if (app_frametime_ns != uint64_t(-1))
    {
        float app_frametime_ms = app_frametime_ns / 1000000.f;
        HUDElements.gamescope_debug_app.push_back(app_frametime_ms);
        if (HUDElements.gamescope_debug_app.size() > 200)
            HUDElements.gamescope_debug_app.erase(HUDElements.gamescope_debug_app.begin());
    }

    float latency_ms = latency_ns / 1000000.f;
    if (latency_ns == uint64_t(-1))
        latency_ms = -1;
    HUDElements.gamescope_debug_latency.push_back(latency_ms);
    if (HUDElements.gamescope_debug_latency.size() > 200)
        HUDElements.gamescope_debug_latency.erase(HUDElements.gamescope_debug_latency.begin());
}

static void msg_read_thread(){
    for (size_t i = 0; i < 200; i++){
        HUDElements.gamescope_debug_app.push_back(0);
        HUDElements.gamescope_debug_latency.push_back(0);
    }
    int key = ftok("mangoapp", 65);
    msgid = msgget(key, 0666 | IPC_CREAT);
    // uint32_t previous_pid = 0;
    const struct mangoapp_msg_header *hdr = (const struct mangoapp_msg_header*) raw_msg;
    const struct mangoapp_msg_v1 *mangoapp_v1 = (const struct mangoapp_msg_v1*) raw_msg;
    while (1){
        // make sure that the message recieved is compatible
        // and that we're not trying to use variables that don't exist (yet)
        size_t msg_size = msgrcv(msgid, (void *) raw_msg, sizeof(raw_msg), 1, 0);
        if (msg_size != size_t(-1))
        {
            if (hdr->version == 1){
                if (msg_size > offsetof(struct mangoapp_msg_v1, visible_frametime_ns)){
                    bool should_new_frame = false;
                    if (mangoapp_v1->visible_frametime_ns != ~(0lu) && (!params.no_display || logger->is_active())) {
                        update_hud_info_with_frametime(sw_stats, params, vendorID, mangoapp_v1->visible_frametime_ns);
                        should_new_frame = true;
                    }

                    if (msg_size > offsetof(mangoapp_msg_v1, fsrUpscale)){
                        HUDElements.g_fsrUpscale = mangoapp_v1->fsrUpscale;
                        if (params.fsr_steam_sharpness < 0)
                            HUDElements.g_fsrSharpness = mangoapp_v1->fsrSharpness;
                        else
                        HUDElements.g_fsrSharpness = params.fsr_steam_sharpness - mangoapp_v1->fsrSharpness;
                    }
                    if (!HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_mangoapp_steam]){
                        steam_focused = mangoapp_v1->bSteamFocused;
                    } else {
                        steam_focused = false;
                    }
                    HUDElements.hdr_status = mangoapp_v1->bAppWantsHDR;
                    HUDElements.refresh = mangoapp_v1->displayRefresh;
                    // if (!steam_focused && mangoapp_v1->pid != previous_pid){
                    //     string path = "/tmp/mangoapp/" + to_string(mangoapp_v1->pid) + ".json";
                    //     ifstream i(path);
                    //     if (i.fail()){
                    //         sw_stats.engine = EngineTypes::GAMESCOPE;
                    //     } else {
                    //         json j;
                    //         i >> j;
                    //         sw_stats.engine = static_cast<EngineTypes> (j["engine"]);
                    //     }
                    //     previous_pid = mangoapp_v1->pid;
                    // }
                    if (msg_size > offsetof(mangoapp_msg_v1, latency_ns))
                        gamescope_frametime(mangoapp_v1->app_frametime_ns, mangoapp_v1->latency_ns);

                    if (should_new_frame)
                    {
                        {
                            std::unique_lock<std::mutex> lk(mangoapp_m);
                            new_frame = true;
                        }
                        mangoapp_cv.notify_one();
                        screenWidth = mangoapp_v1->outputWidth;
                        // Don't care about the height, just the width.
                        //screenHeight = mangoapp_v1->outputHeight;
                    }
                }
            } else {
                printf("Unsupported mangoapp struct version: %i\n", hdr->version);
                exit(1);
            }
        }
        else
        {
            printf("mangoapp: msgrcv returned -1 with error %d - %s\n", errno, strerror(errno));
        }
    }
}

static bool make_layer_shell(GLFWwindow *window)
{
    wl_display *display = glfwGetWaylandDisplay();
    if (!display)
        return false;

    wl_registry *registry;
    if (!(registry = wl_display_get_registry(display)))
        return false;

    struct registry_data
    {
        struct zwlr_layer_shell_v1 *layer_shell = nullptr;
    } data;

    static const wl_registry_listener s_RegistryListener =
    {
        .global = [] ( void *pUserData, wl_registry *pRegistry, uint32_t uName, const char *pInterface, uint32_t uVersion )
        {
            registry_data *data = (registry_data *)pUserData;
            if ( !strcmp( pInterface, zwlr_layer_shell_v1_interface.name ) && uVersion >= 4u )
                data->layer_shell = (zwlr_layer_shell_v1 *)wl_registry_bind(pRegistry, uName, &zwlr_layer_shell_v1_interface, 4 );
        },
        .global_remove = []( auto... args )
        {
        },
    };

    wl_registry_add_listener( registry, &s_RegistryListener, (void *)&data );
    wl_display_roundtrip(display);

    if ( !data.layer_shell )
    {
        fprintf(stderr, "Failed to get layer shell!\n");
        return false;
    }

    wl_surface *surface = glfwGetWaylandWindow(window);

    layer_surface = zwlr_layer_shell_v1_get_layer_surface(data.layer_shell, surface, nullptr, 1, "performance_overlay");
    if (!layer_surface)
    {
        fprintf(stderr, "Failed to create layer surface!\n");
        return false;
    }

    static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
        .configure = [](void *data, struct zwlr_layer_surface_v1 *layer_surface, uint32_t serial, uint32_t width, uint32_t height)
        {
            SPDLOG_INFO("CONFIGURE!!!");
            wl_surface *surface = (wl_surface *)data;
            zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
            screenWidth = width;
            screenHeight = height;
            if ( !screenWidth )
                screenWidth = 1280;
            if ( !screenHeight )
                screenHeight = 800;
            zwlr_layer_surface_v1_set_size(layer_surface, width, height);
            wl_surface_commit(surface);
        },
        .closed = [](void *data, struct zwlr_layer_surface_v1 *surface)
        {
        },
    };

    zwlr_layer_surface_v1_add_listener(layer_surface, &layer_surface_listener, surface);

    zwlr_layer_surface_v1_set_size(layer_surface, screenWidth, screenHeight);
    zwlr_layer_surface_v1_set_anchor(layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_margin(layer_surface, 0, 0, 0, 0);

    wl_display_roundtrip(display);

    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    return true;
}

static GLFWwindow* init(const char* glsl_version){
    init_spdlog();

    // Don't make xdg_shell objects for us.
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow *window = glfwCreateWindow(1280, 800, "mangoapp overlay window", NULL, NULL);
    if ( !make_layer_shell(window) )
        return nullptr;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync
    ImGui::CreateContext();
#ifdef __linux__
    ImPlot::CreateContext();
#endif
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = NULL;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
    return window;
}

static void shutdown(GLFWwindow* window){
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
}

static bool render(GLFWwindow* window) {
    if (HUDElements.colors.update)
        HUDElements.convert_colors(params);

    ImVec2 last_window_size = window_size;
    ImGui_ImplGlfw_NewFrame();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    overlay_new_frame(params);
    position_layer(sw_stats, params, window_size);
    render_imgui(sw_stats, params, window_size, true);
    overlay_end_frame();
    if (screenWidth && screenHeight)
    {
        zwlr_layer_surface_v1_set_size(layer_surface, screenWidth, screenHeight);
        glfwSetWindowSize(window, screenWidth, screenHeight);
    }
    ImGui::EndFrame();

    // We don't re-load these, clear the texture data now.
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->ClearTexData();

    return last_window_size.x != window_size.x || last_window_size.y != window_size.y;
}

int main(int, char**)
{
    // If we are under Gamescope, always prefer running under Wayland.
    const char *gamescope_wayland_display = getenv( "GAMESCOPE_WAYLAND_DISPLAY");
    if (gamescope_wayland_display && *gamescope_wayland_display)
    {
        setenv("WAYLAND_DISPLAY", gamescope_wayland_display, 1);
        unsetenv("DISPLAY");
    }

    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    glfwWindowHint(GLFW_RESIZABLE, 1);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, 1);

    // Create window with graphics context
    GLFWwindow* window = init(glsl_version);
    // Initialize OpenGL loader

    bool err = glewInit() != GLEW_OK;

    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    // Setup Platform/Renderer backends
    int control_client = -1;
    parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"), false);
    create_fonts(nullptr, params, sw_stats.font1, sw_stats.font_text);
    HUDElements.convert_colors(params);
    init_cpu_stats(params);
    notifier.params = &params;
    start_notifier(notifier);
    window_size = ImVec2(params.width, params.height);
    deviceName = (char*)glGetString(GL_RENDERER);
    sw_stats.deviceName = deviceName;
    SPDLOG_DEBUG("mangoapp deviceName: {}", deviceName);
    #define GLX_RENDERER_VENDOR_ID_MESA 0x8183
    auto pfn_glXQueryCurrentRendererIntegerMESA = (Bool (*)(int, unsigned int*)) (glfwGetProcAddress("glXQueryCurrentRendererIntegerMESA"));
    // This will return 0x0 vendorID on NVIDIA so just go to else
    if (pfn_glXQueryCurrentRendererIntegerMESA && vendorID != 0x0) {
        pfn_glXQueryCurrentRendererIntegerMESA(GLX_RENDERER_VENDOR_ID_MESA, &vendorID);
        SPDLOG_DEBUG("mangoapp vendorID: {:#x}", vendorID);
    } else {
        if (deviceName.find("Radeon") != std::string::npos
        || deviceName.find("AMD") != std::string::npos){
            vendorID = 0x1002;
        } else if (deviceName.find("Intel") != std::string::npos) {
            vendorID = 0x8086;
        } else {
            vendorID = 0x10de;
        }
    }

    HUDElements.vendorID = vendorID;
    init_gpu_stats(vendorID, 0, params);
    init_system_info();
    sw_stats.engine = EngineTypes::GAMESCOPE;
    std::thread(msg_read_thread).detach();
    std::thread(ctrl_thread).detach();
    if(!logger) logger = std::make_unique<Logger>(HUDElements.params);
    // Main loop
    while (!glfwWindowShouldClose(window)){
        if (!params.no_display){
            if (mangoapp_paused){
                mangoapp_paused = false;
                {
                    amdgpu_run_thread = true;
                    amdgpu_c.notify_one();
                }
            }
            {
                std::unique_lock<std::mutex> lk(mangoapp_m);
                mangoapp_cv.wait(lk, []{return new_frame || params.no_display;});
                new_frame = false;
            }

            check_keybinds(params, vendorID);
            // Start the Dear ImGui frame
            {
                if (render(window)) {
                    // If we need to resize our window, give it another couple of rounds for the
                    // stupid display size stuff to propagate through ImGUI (using NDC and scaling
                    // in GL makes me a very unhappy boy.)
                    render(window);
                    render(window);
                }

                if (params.control >= 0) {
                    control_client_check(params.control, control_client, deviceName);
                    process_control_socket(control_client, params);
                }
            }
            // Rendering
            ImGui::Render();
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            eglSwapBuffers(glfwGetEGLDisplay(), glfwGetEGLSurface(window));
        } else if (!mangoapp_paused) {
            mangoapp_paused = true;
            {
                amdgpu_run_thread = false;
                amdgpu_c.notify_one();
            }
            std::unique_lock<std::mutex> lk(mangoapp_m);
            mangoapp_cv.wait(lk, []{return !params.no_display;});
        }
    }

    // Cleanup
    SPDLOG_INFO("Mangohud Shutting Down");
    shutdown(window);

    glfwTerminate();

    return 0;
}
