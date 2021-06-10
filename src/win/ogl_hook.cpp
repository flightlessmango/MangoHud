#include "kiero.h"
#include <stdio.h>
#include <assert.h>
#include "windows.h"
#include <GL/gl.h>
#include "d3d_shared.h"

BOOL __stdcall (*owglSwapBuffers)(HDC hDc);

BOOL __stdcall hwglSwapBuffers(HDC hDc)
{
    d3d_run();
    return owglSwapBuffers(hDc);
}

void init_ogl(){
    printf("init ogl\n");
	auto ret = kiero::bind(336, (void**)&owglSwapBuffers, reinterpret_cast<void *>(hwglSwapBuffers));
	if(ret != kiero::Status::Success)
        printf("not opengl\n");
    else
        init_d3d_shared();
}