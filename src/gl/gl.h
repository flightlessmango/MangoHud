#pragma once

#ifdef __cplusplus
extern "C" {
#endif

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
#endif
