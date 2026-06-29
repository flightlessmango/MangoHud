#pragma once
#ifndef MANGOHUD_GL_GL_H
#define MANGOHUD_GL_GL_H

#include <stdint.h>

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned char GLubyte;
typedef float GLfloat;

#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RENDERER 0x1F01

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

const GLubyte* glGetString(GLenum name);
void glEnable(GLenum cap);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void glClear(GLbitfield mask);

void * glXCreateContext(void *, void *, void *, int);
void glXDestroyContext(void *, void*);
void glXSwapBuffers(void*, void*);
void glXSwapIntervalEXT(void*, void*, int);
int glXSwapIntervalSGI(int);
int glXSwapIntervalMESA(unsigned int);
int glXGetSwapIntervalMESA(void);
int glXMakeContextCurrent(void*, void*, void*, void*);
int glXMakeCurrent(void*, void*, void*);
void *glXGetCurrentContext();
void *glXGetCurrentDrawable();
void *glXGetCurrentReadDrawable();
void *glXCreateContextAttribsARB(void *dpy, void *config,void *share_context, int direct, const int *attrib_list);

void* glXGetProcAddress(const unsigned char*);
void* glXGetProcAddressARB(const unsigned char*);
int glXQueryDrawable(void *dpy, void* glxdraw, int attr, unsigned int * value);

int64_t glXSwapBuffersMscOML(void* dpy, void* drawable, int64_t target_msc, int64_t divisor, int64_t remainder);

unsigned int eglSwapBuffers( void*, void* );
void* eglGetPlatformDisplay( unsigned int, void*, const intptr_t* );
void* eglGetDisplay( void* );
unsigned int eglDestroyContext( void*, void* );
void* eglGetProcAddress( const char* );
int eglTerminate( void* );

void* eglGetPlatformDisplayEXT(unsigned int platform, void* native_display, const intptr_t* attrib_list);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //MANGOHUD_GL_GL_H
