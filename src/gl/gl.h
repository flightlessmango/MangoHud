#pragma once
#ifndef MANGOHUD_GL_GL_H
#define MANGOHUD_GL_GL_H

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

void * glXCreateContext(void *, void *, void *, int);
void glXDestroyContext(void *, void*);
void glXSwapBuffers(void*, void*);
void glXSwapIntervalEXT(void*, void*, int);
int glXSwapIntervalSGI(int);
int glXSwapIntervalMESA(unsigned int);
int glXGetSwapIntervalMESA(void);
int glXMakeCurrent(void*, void*, void*);
void* glXGetCurrentContext();

void* glXGetProcAddress(const unsigned char*);
void* glXGetProcAddressARB(const unsigned char*);
int glXQueryDrawable(void *dpy, void* glxdraw, int attr, unsigned int * value);

int64_t glXSwapBuffersMscOML(void* dpy, void* drawable, int64_t target_msc, int64_t divisor, int64_t remainder);

unsigned int eglSwapBuffers( void*, void* );

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //MANGOHUD_GL_GL_H
