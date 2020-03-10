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
bool glXMakeCurrent(void*, void*, void*);

void* glXGetProcAddress(const unsigned char*);
void* glXGetProcAddressARB(const unsigned char*);

unsigned int eglSwapBuffers( void*, void* );

void glClipControl(int origin, int depth);

#ifdef __cplusplus
}
#endif
