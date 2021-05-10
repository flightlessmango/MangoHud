#include "kiero.h"
#include <stdio.h>
#include <assert.h>
#include "windows.h"
#include <GL/gl.h>

BOOL __stdcall (*owglSwapBuffers)(HDC hDc);

BOOL __stdcall hwglSwapBuffers(HDC hDc)
{
    printf("swapbuffer\n");
    return owglSwapBuffers(hDc);
}

void init_ogl(){
    printf("init ogl\n");
	auto ret = kiero::bind(336, (void**)&owglSwapBuffers, reinterpret_cast<void *>(hwglSwapBuffers));
    assert(ret == kiero::Status::Success);
}