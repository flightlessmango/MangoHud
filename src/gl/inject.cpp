#include <iostream>
#include <array>
#include <unordered_map>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <string>
#include "real_dlsym.h"
#include "loaders/loader_gl.h"
#include "GL/gl3w.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "font_default.h"
#include "overlay.h"
#include "mesa/util/macros.h"
#include "mesa/util/os_time.h"

#include <chrono>
#include <iomanip>

#define EXPORT_C_(type) extern "C" __attribute__((__visibility__("default"))) type

EXPORT_C_(void *) glXGetProcAddress(const unsigned char* procName);
EXPORT_C_(void *) glXGetProcAddressARB(const unsigned char* procName);

gl_loader gl;

struct state {
    ImGuiContext *imgui_ctx = nullptr;
    ImFont* font = nullptr;
    ImFont* font1 = nullptr;
};

static ImVec2 window_size;
static overlay_params params {};
static swapchain_stats sw_stats {};
static fps_limit fps_limit_stats {};
static state *current_state; 
static bool inited = false;
std::unordered_map<void*, state> g_imgui_states;
uint32_t vendorID;
std::string deviceName;

void imgui_init()
{
    if (inited)
        return;
    inited = true;
    parse_overlay_config(&params, getenv("MANGOHUD_CONFIG"));
    window_size = ImVec2(params.width, params.height);
    init_system_info();
    if (params.fps_limit > 0)
      fps_limit_stats.targetFrameTime = int64_t(1000000000.0 / params.fps_limit);
}

void imgui_create(void *ctx)
{
    if (!ctx)
        return;
    imgui_init();
    gl3wInit();
    std::cerr << "GL version: " << glGetString(GL_VERSION) << std::endl;
    deviceName = (char*)glGetString(GL_RENDERER);
    if (deviceName.find("Radeon") != std::string::npos
    || deviceName.find("AMD") != std::string::npos){
        vendorID = 0x1002;
    } else {
        vendorID = 0x10de;
    }
    init_gpu_stats(vendorID, params);
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    auto& state = g_imgui_states[ctx];
    state.imgui_ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();
    imgui_custom_style();
    
    GLint vp [4]; glGetIntegerv (GL_VIEWPORT, vp);
    printf("viewport %d %d %d %d\n", vp[0], vp[1], vp[2], vp[3]);
    ImGui::GetIO().IniFilename = NULL;
    ImGui::GetIO().DisplaySize = ImVec2(vp[2], vp[3]);

    ImGui_ImplOpenGL3_Init();
    // Make a dummy GL call (we don't actually need the result)
    // IF YOU GET A CRASH HERE: it probably means that you haven't initialized the OpenGL function loader used by this code.
    // Desktop OpenGL 3/4 need a function loader. See the IMGUI_IMPL_OPENGL_LOADER_xxx explanation above.
    GLint current_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &current_texture);

    int font_size = params.font_size;
    if (!font_size)
        font_size = 24;

    ImFontConfig font_cfg = ImFontConfig();
    const char* ttf_compressed_base85 = GetDefaultCompressedFontDataTTFBase85();
    const ImWchar* glyph_ranges = io.Fonts->GetGlyphRangesDefault();

    state.font = io.Fonts->AddFontFromMemoryCompressedBase85TTF(ttf_compressed_base85, font_size, &font_cfg, glyph_ranges);
    state.font1 = io.Fonts->AddFontFromMemoryCompressedBase85TTF(ttf_compressed_base85, font_size * 0.55, &font_cfg, glyph_ranges);
    current_state = &state;
    engineName = "OpenGL";
}

void imgui_destroy(void *ctx)
{
    if (!ctx)
        return;

    auto it = g_imgui_states.find(ctx);
    if (it != g_imgui_states.end()) {
        ImGui::DestroyContext(it->second.imgui_ctx);
        g_imgui_states.erase(it);
    }
}

void imgui_shutdown()
{
    std::cerr << __func__ << std::endl;

    ImGui_ImplOpenGL3_Shutdown();

    for(auto &p : g_imgui_states)
        ImGui::DestroyContext(p.second.imgui_ctx);
    g_imgui_states.clear();
}

void imgui_set_context(void *ctx)
{
    if (!ctx) {
        imgui_shutdown();
        current_state = nullptr;
        return;
    }
    std::cerr << __func__ << std::endl;

    auto it = g_imgui_states.find(ctx);
    if (it != g_imgui_states.end()) {
        ImGui::SetCurrentContext(it->second.imgui_ctx);
        current_state = &it->second;
    } else {
        imgui_create(ctx);
    }
    sw_stats.font1 = current_state->font1;
}

void imgui_render()
{
    if (!ImGui::GetCurrentContext())
        return;
    GLint vp [4]; glGetIntegerv (GL_VIEWPORT, vp);
    ImGui::GetIO().DisplaySize = ImVec2(vp[2], vp[3]);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    position_layer(params, window_size, vp[2], vp[3]);
    render_imgui(sw_stats, params, window_size, vp[2], vp[3]);
    ImGui::PopStyleVar(2);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void* get_proc_address(const char* name) {
    void (*func)() = (void (*)())real_dlsym( RTLD_NEXT, name );

    if (!func) {
        std::cerr << "MangoHud: Failed to get function '" << name << "'" << std::endl;
        exit( 1 );
    }

    return (void*)func;
}

void* get_glx_proc_address(const char* name) {
    gl.Load();

    void *func = gl.glXGetProcAddress( (const unsigned char*) name );

    if (!func)
        func = gl.glXGetProcAddressARB( (const unsigned char*) name );

    if (!func)
        func = get_proc_address( name );

    return func;
}

EXPORT_C_(void *) glXCreateContext(void *dpy, void *vis, void *shareList, int direct)
{
    gl.Load();
    void *ctx = gl.glXCreateContext(dpy, vis, shareList, direct);
    std::cerr << __func__ << ":" << ctx << std::endl;
    return ctx;
}

EXPORT_C_(bool) glXMakeCurrent(void* dpy, void* drawable, void* ctx) {
    gl.Load();
    bool ret = gl.glXMakeCurrent(dpy, drawable, ctx);
    if (ret)
        imgui_set_context(ctx);
    std::cerr << __func__ << std::endl;
    return ret;
}

EXPORT_C_(void) glXSwapBuffers(void* dpy, void* drawable) {
    gl.Load();
    check_keybinds(params);
    update_hud_info(sw_stats, params, vendorID);
    imgui_render();
    gl.glXSwapBuffers(dpy, drawable);
    if (fps_limit_stats.targetFrameTime > 0){
        fps_limit_stats.frameStart = os_time_get_nano();
        FpsLimiter(fps_limit_stats);
        fps_limit_stats.frameEnd = os_time_get_nano();
    }
}

struct func_ptr {
   const char *name;
   void *ptr;
};

static std::array<const func_ptr, 5> name_to_funcptr_map = {{
#define ADD_HOOK(fn) { #fn, (void *) fn }
   ADD_HOOK(glXGetProcAddress),
   ADD_HOOK(glXGetProcAddressARB),
   ADD_HOOK(glXCreateContext),
   ADD_HOOK(glXMakeCurrent),
   ADD_HOOK(glXSwapBuffers),
#undef ADD_HOOK
}};

static void *find_ptr(const char *name)
{
   for (auto& func : name_to_funcptr_map) {
      if (strcmp(name, func.name) == 0)
         return func.ptr;
   }

   return nullptr;
}

EXPORT_C_(void *) glXGetProcAddress(const unsigned char* procName) {
    gl.Load();

    //std::cerr << __func__ << ":" << procName << std::endl;

    void* func = find_ptr( (const char*)procName );
    if (func)
        return func;

    return gl.glXGetProcAddress(procName);
}

EXPORT_C_(void *) glXGetProcAddressARB(const unsigned char* procName) {
    gl.Load();

    //std::cerr << __func__ << ":" << procName << std::endl;

    void* func = find_ptr( (const char*)procName );
    if (func)
        return func;

    return gl.glXGetProcAddressARB(procName);
}

#ifdef HOOK_DLSYM
EXPORT_C_(void*) dlsym(void * handle, const char * name)
{
    void* func = find_ptr(name);
    if (func) {
        //std::cerr << __func__ << ":" << name << std::endl;
        return func;
    }

    //std::cerr << __func__ << ": foreign: " << name << std::endl;
    return real_dlsym(handle, name);
}
#endif