#pragma once
#include "gl/gl.h"
#include <dlfcn.h>

class glx_loader {
 public:
  glx_loader();
  ~glx_loader();

  bool Load();
  bool IsLoaded() { return loaded_; }

  decltype(&::glXGetProcAddress) GetProcAddress;
  decltype(&::glXGetProcAddressARB) GetProcAddressARB;
  decltype(&::glXCreateContext) CreateContext;
  decltype(&::glXCreateContextAttribsARB) CreateContextAttribs;
  decltype(&::glXCreateContextAttribsARB) CreateContextAttribsARB;
  decltype(&::glXDestroyContext) DestroyContext;
  decltype(&::glXSwapBuffers) SwapBuffers;
  decltype(&::glXSwapIntervalEXT) SwapIntervalEXT;
  decltype(&::glXSwapIntervalSGI) SwapIntervalSGI;
  decltype(&::glXSwapIntervalMESA) SwapIntervalMESA;
  decltype(&::glXGetSwapIntervalMESA) GetSwapIntervalMESA;
  decltype(&::glXMakeCurrent) MakeCurrent;
  decltype(&::glXGetCurrentContext) GetCurrentContext;
  decltype(&::glXQueryDrawable) QueryDrawable;
  decltype(&::glXSwapBuffersMscOML) SwapBuffersMscOML;

 private:
  void CleanUp(bool unload);

  bool loaded_;

  // Disallow copy constructor and assignment operator.
  glx_loader(const glx_loader&);
  void operator=(const glx_loader&);
};
