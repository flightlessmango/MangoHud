#include "kiero.h"
#include "windows.h"
#include <cstdio>

void ConsoleSetup()
{
	// With this trick we'll be able to print content to the console, and if we have luck we could get information printed by the game.
	AllocConsole();
	SetConsoleTitle("MangoHud");
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
	freopen("CONIN$", "r", stdin);
}

int MainThread(){
    ConsoleSetup();
    printf("MangoHud Attached!\n");
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