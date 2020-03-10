#ifndef LIBRARY_LOADER_GL_H
#define LIBRARY_LOADER_GL_H

#include "gl/gl.h"
#include <dlfcn.h>

class gl_loader {
 public:
  gl_loader();
  ~gl_loader();

  bool Load(bool egl_only = false);
  bool IsLoaded() { return loaded_; }

  decltype(&::glXGetProcAddress) glXGetProcAddress;
  decltype(&::glXGetProcAddressARB) glXGetProcAddressARB;
  decltype(&::glXCreateContext) glXCreateContext;
  decltype(&::glXDestroyContext) glXDestroyContext;
  decltype(&::glXSwapBuffers) glXSwapBuffers;
  decltype(&::glXSwapIntervalEXT) glXSwapIntervalEXT;
  decltype(&::glXSwapIntervalSGI) glXSwapIntervalSGI;
  decltype(&::glXSwapIntervalMESA) glXSwapIntervalMESA;
  decltype(&::glXMakeCurrent) glXMakeCurrent;

  decltype(&::glClipControl) glClipControl;

  decltype(&::eglSwapBuffers) eglSwapBuffers;

 private:
  void CleanUp(bool unload);

  bool loaded_;

  // Disallow copy constructor and assignment operator.
  gl_loader(const gl_loader&);
  void operator=(const gl_loader&);
};

#endif  // LIBRARY_LOADER_GL_H
