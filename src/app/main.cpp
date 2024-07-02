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
#include "notify.h"
#include "mangoapp.h"
#include "mangoapp_proto.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "amdgpu.h"
#ifdef __linux__
#include "implot.h"
#endif

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xatom.h>

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
static uint32_t screenWidth, screenHeight;

static unsigned int get_prop(const char* propName){
    Display *x11_display = glfwGetX11Display();
    Atom gamescope_focused = XInternAtom(x11_display, propName, false);
    auto scr = DefaultScreen(x11_display);
    auto root = RootWindow(x11_display, scr);
    Atom actual;
    int format;
    unsigned long n, left;
    uint64_t *data;
    int result = XGetWindowProperty(x11_display, root, gamescope_focused, 0L, 1L, false,
                            XA_CARDINAL, &actual, &format,
                            &n, &left, ( unsigned char** )&data);

    if (result == Success && data != NULL){
        bool *found = nullptr;
        unsigned int i;
        memcpy(&i, data, sizeof(unsigned int));
        XFree((void *) data);
        if ( found != nullptr )
        {
            *found = true;
        }
        return i;
    }
    return -1;
}

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
                        steam_focused = get_prop("GAMESCOPE_FOCUSED_APP_GFX") == 769;
                    } else {
                        steam_focused = false;
                    }
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
                        screenHeight = mangoapp_v1->outputHeight;
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

static const char *GamescopeOverlayProperty = "GAMESCOPE_EXTERNAL_OVERLAY";

static GLFWwindow* init(const char* glsl_version){
    init_spdlog();
    GLFWwindow *window = glfwCreateWindow(1280, 800, "mangoapp overlay window", NULL, NULL);
    Display *x11_display = glfwGetX11Display();
    Window x11_window = glfwGetX11Window(window);
    if (x11_window && x11_display)
    {
        // Set atom for gamescope to render as an overlay.
        Atom overlay_atom = XInternAtom (x11_display, GamescopeOverlayProperty, False);
        uint32_t value = 1;
        XChangeProperty(x11_display, x11_window, overlay_atom, XA_CARDINAL, 32, PropertyNewValue, (unsigned char *)&value, 1);
    }

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

static void get_atom_info(){
    HUDElements.hdr_status = get_prop("GAMESCOPE_COLOR_APP_WANTS_HDR_FEEDBACK");
    HUDElements.refresh = get_prop("GAMESCOPE_DISPLAY_REFRESH_RATE_FEEDBACK");
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
    get_atom_info();
    overlay_end_frame();
    if (screenWidth && screenHeight)
        glfwSetWindowSize(window, screenWidth, screenHeight);
    ImGui::EndFrame();
    return last_window_size.x != window_size.x || last_window_size.y != window_size.y;
}

int main(int, char**)
{
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

    Display *x11_display = glfwGetX11Display();
    Window x11_window = glfwGetX11Window(window);
    Atom overlay_atom = XInternAtom (x11_display, GamescopeOverlayProperty, False);
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
    Atom noFocusAtom = XInternAtom(x11_display, "GAMESCOPE_NO_FOCUS", False);
    uint32_t value = 1;
    XChangeProperty(x11_display, x11_window, noFocusAtom, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&value, 1);
    // Main loop
    while (!glfwWindowShouldClose(window)){
        if (!params.no_display){
            if (mangoapp_paused){
                glfwRestoreWindow(window);
                uint32_t value = 1;
                XChangeProperty(x11_display, x11_window, overlay_atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&value, 1);
                XSync(x11_display, 0);
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

            glfwSwapBuffers(window);
        } else if (!mangoapp_paused) {
            glfwIconifyWindow(window);
            uint32_t value = 0;
            XChangeProperty(x11_display, x11_window, overlay_atom, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&value, 1);
            XSync(x11_display, 0);
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
    shutdown(window);

    glfwTerminate();

    return 0;
}
