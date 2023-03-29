#include <cstdio>
#include "kiero.h"
#include <vector>
#include "win_shared.h"
#if KIERO_INCLUDE_D3D11
# include "d3d11_hook.h"
#endif
#if KIERO_INCLUDE_D3D12
# include "d3d12_hook.h"
#endif

#ifdef _UNICODE
# define KIERO_TEXT(text) L##text
#else
# define KIERO_TEXT(text) text
#endif

std::vector<kiero::RenderType::Enum> render_types;

void ConsoleSetup()
{
	// With this trick we'll be able to print content to the console, and if we have luck we could get information printed by the game.
	AllocConsole();
	SetConsoleTitle("MangoHud");
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
	freopen("CONIN$", "r", stdin);
}

void renderTypes() {
    render_types.clear();
    if (::GetModuleHandle(KIERO_TEXT("d3d9.dll")) != NULL)
    {
        render_types.push_back(kiero::RenderType::D3D9);
    }
    if (::GetModuleHandle(KIERO_TEXT("d3d10.dll")) != NULL)
    {
        render_types.push_back(kiero::RenderType::D3D10);
    }
    if (::GetModuleHandle(KIERO_TEXT("d3d11.dll")) != NULL)
    {
        render_types.push_back(kiero::RenderType::D3D11);
    }
    if (::GetModuleHandle(KIERO_TEXT("d3d12.dll")) != NULL)
    {
        render_types.push_back(kiero::RenderType::D3D12);
    }
    if (::GetModuleHandle(KIERO_TEXT("opengl32.dll")) != NULL)
    {
        render_types.push_back(kiero::RenderType::OpenGL);
    }
    for (auto& _type : render_types)
        kiero::init(_type);
}

int MainThread()
{
    ConsoleSetup();
    printf("MangoHud Attached!\n");
    renderTypes();
    if (!render_types.empty()){
        impl::d3d11::init();
        impl::d3d12::init();
        return 1;
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID)
{
    
    DisableThreadLibraryCalls(hInstance);

    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainThread, NULL, 0, NULL);
        break;
    }

    return TRUE;
}