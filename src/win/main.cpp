#include "windows.h"
#include <cstdio>
#include "kiero.h"

#if KIERO_INCLUDE_D3D12
# include "d3d12_hook.h"
#endif

void ConsoleSetup()
{
	// With this trick we'll be able to print content to the console, and if we have luck we could get information printed by the game.
	AllocConsole();
	SetConsoleTitle("MangoHud");
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
	freopen("CONIN$", "r", stdin);
}

int MainThread()
{
    ConsoleSetup();
    printf("MangoHud Attached!\n");
    if (kiero::init(kiero::RenderType::Auto) == kiero::Status::Success)
    {
        switch (kiero::getRenderType())
        {
#if KIERO_INCLUDE_D3D12
        case kiero::RenderType::D3D12:
            impl::d3d12::init();
            break;
#endif
        }

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