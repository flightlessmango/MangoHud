// Dear ImGui: standalone example application for GLFW + OpenGL 3, using programmable pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <thread>
#include <unistd.h>
#include "../overlay.h"
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

struct mangoapp_msg_header {
    long msg_type;  // Message queue ID, never change
    uint32_t version;  /* for major changes in the way things work */
} __attribute__((packed));

struct mangoapp_msg_v1 {
    struct mangoapp_msg_header hdr;
    
    uint32_t pid;
    uint64_t frametime_ns;
    // WARNING: Always ADD fields, never remove or repurpose fields
} __attribute__((packed));

static uint8_t raw_msg[1024] = {0};

void msg_read_thread(){
    int key = ftok("mangoapp", 65);
    int msgid = msgget(key, 0666 | IPC_CREAT);
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

static const char *SteamOverlayProperty = "STEAM_OVERLAY";

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
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Dear ImGui GLFW+OpenGL3 example", NULL, NULL);
    if (window == NULL)
        return 1;

    Display *x11_display = glfwGetX11Display();
    Window x11_window = glfwGetX11Window(window);
    if (x11_window && x11_display)
    {
        // Set atom for gamescope to render as an overlay.
        Atom overlay_atom = XInternAtom (x11_display, SteamOverlayProperty, False);
        uint32_t value = 1;
        XChangeProperty(x11_display, x11_window, overlay_atom, XA_ATOM, 32, PropertyNewValue, (unsigned char *)&value, 1);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Initialize OpenGL loader

    bool err = glewInit() != GLEW_OK;

    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
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
    init_gpu_stats(vendorID, params);
    init_system_info();
    sw_stats.engine = EngineTypes::GAMESCOPE;
    std::thread(msg_read_thread).detach();
    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        window_size.x = window_size.x * 1.1;
        ImGui::NewFrame();
        {
            check_keybinds(sw_stats, params, vendorID);
            position_layer(sw_stats, params, window_size);
            render_imgui(sw_stats, params, window_size, true);
        }
        ImGui::PopStyleVar(3);

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glEnable(GL_DEPTH_TEST);        
        glEnable(GL_BLEND);             
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
