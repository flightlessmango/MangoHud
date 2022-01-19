// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <thread>
#include <unistd.h>
#include "../overlay.h"
#include "mangoapp.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xatom.h>

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
int msgid;
bool mangoapp_paused = false;
std::mutex mangoapp_m;
std::condition_variable mangoapp_cv;
static uint8_t raw_msg[1024] = {0};

void msg_read_thread(){
    int key = ftok("mangoapp", 65);
    msgid = msgget(key, 0666 | IPC_CREAT);
    const struct mangoapp_msg_header *hdr = (const struct mangoapp_msg_header*) raw_msg;
    const struct mangoapp_msg_v1 *mangoapp_v1 = (const struct mangoapp_msg_v1*) raw_msg;
    while (1){
        // make sure that the message recieved is compatible
        // and that we're not trying to use variables that don't exist (yet)
        size_t msg_size = msgrcv(msgid, (void *) raw_msg, sizeof(raw_msg), 1, 0);
        if (hdr->version == 1){
            if (msg_size > offsetof(struct mangoapp_msg_v1, frametime_ns)){
                update_hud_info_with_frametime(sw_stats, params, vendorID, mangoapp_v1->frametime_ns);
            }
        } else {
            printf("Unsupported mangoapp struct version: %i\n", hdr->version);
            exit(1);
        }
    }
}

static const char *GamescopeOverlayProperty = "GAMESCOPE_EXTERNAL_OVERLAY";

GLFWwindow* init(GLFWwindow* window, const char* glsl_version){
    window = glfwCreateWindow(1280, 720, "mangoapp overlay window", NULL, NULL);
    Display *x11_display = glfwGetX11Display();
    Window x11_window = glfwGetX11Window(window);
    if (x11_window && x11_display)
    {
        // Set atom for gamescope to render as an overlay.
        Atom overlay_atom = XInternAtom (x11_display, GamescopeOverlayProperty, False);
        uint32_t value = 1;
        XChangeProperty(x11_display, x11_window, overlay_atom, XA_ATOM, 32, PropertyNewValue, (unsigned char *)&value, 1);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
    return window;
}

void shutdown(GLFWwindow* window){
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
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
    GLFWwindow* window = init(window, glsl_version);;
    // Initialize OpenGL loader

    bool err = glewInit() != GLEW_OK;

    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    // Setup Platform/Renderer backends
    parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"));
    create_fonts(params, sw_stats.font1, sw_stats.font_text);
    HUDElements.convert_colors(params);
    init_cpu_stats(params);
    notifier.params = &params;
    start_notifier(notifier);
        deviceName = (char*)glGetString(GL_RENDERER);
    sw_stats.deviceName = deviceName;
    if (deviceName.find("Radeon") != std::string::npos
    || deviceName.find("AMD") != std::string::npos){
        vendorID = 0x1002;
    } else {
        vendorID = 0x10de;
    }
    init_gpu_stats(vendorID, 0, params);
    init_system_info();
    sw_stats.engine = EngineTypes::GAMESCOPE;
    std::thread(msg_read_thread).detach();
    if(!logger) logger = std::make_unique<Logger>(HUDElements.params);
    // Main loop
    while (!glfwWindowShouldClose(window)){
        if (!params.no_display){
            if (mangoapp_paused){
                window = init(window, glsl_version);
                create_fonts(params, sw_stats.font1, sw_stats.font_text);
                HUDElements.convert_colors(params);
                mangoapp_paused = false;
            }
            // Start the Dear ImGui frame
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            window_size.x = window_size.x * 1.2;
            ImGui::NewFrame();
            {
                check_keybinds(sw_stats, params, vendorID);
                position_layer(sw_stats, params, window_size);
                render_imgui(sw_stats, params, window_size, true);
            }
            ImGui::PopStyleVar(3);

            // Rendering
            ImGui::Render();
            static int display_w, display_h;
            if((Clock::now() - logger->last_log_end()) < 12s)
                glfwSetWindowSize(window, window_size.x + 145.f, window_size.y + 325.f);
            else
                glfwSetWindowSize(window, window_size.x + 45.f, window_size.y + 10.f);
            glfwGetFramebufferSize(window, &display_w, &display_h);
            glEnable(GL_DEPTH_TEST);        
            glEnable(GL_BLEND);             
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(window);
        } else if (!mangoapp_paused) {
            shutdown(window);
            mangoapp_paused = true;
            std::unique_lock<std::mutex> lk(mangoapp_m);
            mangoapp_cv.wait(lk, []{return !params.no_display;});
        }
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
