#include <iostream>
#include <spdlog/spdlog.h>
#include "real_dlsym.h"
#include "loaders/loader_glx.h"

glx_loader::glx_loader() : loaded_(false) {
}

glx_loader::~glx_loader() {
  CleanUp(loaded_);
}

bool glx_loader::Load() {
  if (loaded_) {
    return true;
  }

  // Force load libGL
  void *handle = nullptr;
#ifndef NDEBUG
  // Use apitrace's glxtrace.so for debugging
  // Assumes glxtrace.so lives outside of usual library paths and is only preloaded
  handle = real_dlopen("glxtrace.so", RTLD_LAZY);
#endif
  if (!handle)
    handle = real_dlopen("libGL.so.1", RTLD_LAZY);
  if (!handle) {
    SPDLOG_ERROR("Failed to open " MANGOHUD_ARCH " libGL.so.1: {}", dlerror());
    return false;
  }

  GetProcAddress =
      reinterpret_cast<decltype(this->GetProcAddress)>(
          real_dlsym(handle, "glXGetProcAddress"));

  GetProcAddressARB =
      reinterpret_cast<decltype(this->GetProcAddressARB)>(
          real_dlsym(handle, "glXGetProcAddressARB"));

  if (!GetProcAddress) {
    CleanUp(true);
    return false;
  }

  CreateContext =
      reinterpret_cast<decltype(this->CreateContext)>(
          GetProcAddress((const unsigned char *)"glXCreateContext"));
  if (!CreateContext) {
    CleanUp(true);
    return false;
  }

  CreateContextAttribs =
      reinterpret_cast<decltype(this->CreateContextAttribs)>(
          GetProcAddress((const unsigned char *)"glXCreateContextAttribs"));
//   if (!CreateContextAttribs) {
//     CleanUp(true);
//     return false;
//   }

  CreateContextAttribsARB =
      reinterpret_cast<decltype(this->CreateContextAttribsARB)>(
          GetProcAddress((const unsigned char *)"glXCreateContextAttribsARB"));
//   if (!CreateContextAttribsARB) {
//     CleanUp(true);
//     return false;
//   }

  DestroyContext =
      reinterpret_cast<decltype(this->DestroyContext)>(
          GetProcAddress((const unsigned char *)"glXDestroyContext"));
  if (!DestroyContext) {
    CleanUp(true);
    return false;
  }

  GetCurrentContext =
      reinterpret_cast<decltype(this->GetCurrentContext)>(
          GetProcAddress((const unsigned char *)"glXGetCurrentContext"));
  if (!GetCurrentContext) {
    CleanUp(true);
    return false;
  }

  SwapBuffers =
      reinterpret_cast<decltype(this->SwapBuffers)>(
          GetProcAddress((const unsigned char *)"glXSwapBuffers"));
  if (!SwapBuffers) {
    CleanUp(true);
    return false;
  }

  SwapBuffersMscOML =
      reinterpret_cast<decltype(this->SwapBuffersMscOML)>(
          GetProcAddress((const unsigned char *)"glXSwapBuffersMscOML"));
  /*if (!SwapBuffersMscOML) {
    CleanUp(true);
    return false;
  }*/

  SwapIntervalEXT =
      reinterpret_cast<decltype(this->SwapIntervalEXT)>(
          GetProcAddress((const unsigned char *)"glXSwapIntervalEXT"));

  SwapIntervalSGI =
      reinterpret_cast<decltype(this->SwapIntervalSGI)>(
          GetProcAddress((const unsigned char *)"glXSwapIntervalSGI"));

  SwapIntervalMESA =
      reinterpret_cast<decltype(this->SwapIntervalMESA)>(
          GetProcAddress((const unsigned char *)"glXSwapIntervalMESA"));

  GetSwapIntervalMESA =
      reinterpret_cast<decltype(this->GetSwapIntervalMESA)>(
          GetProcAddress((const unsigned char *)"glXGetSwapIntervalMESA"));

  QueryDrawable =
      reinterpret_cast<decltype(this->QueryDrawable)>(
          GetProcAddress((const unsigned char *)"glXQueryDrawable"));

  MakeCurrent =
      reinterpret_cast<decltype(this->MakeCurrent)>(
          GetProcAddress((const unsigned char *)"glXMakeCurrent"));
  if (!MakeCurrent) {
    CleanUp(true);
    return false;
  }

  loaded_ = true;
  return true;
}

void glx_loader::CleanUp(bool unload) {
  loaded_ = false;
  GetProcAddress = nullptr;
  GetProcAddressARB = nullptr;
  CreateContext = nullptr;
  DestroyContext = nullptr;
  SwapBuffers = nullptr;
  SwapIntervalEXT = nullptr;
  SwapIntervalSGI = nullptr;
  SwapIntervalMESA = nullptr;
  QueryDrawable = nullptr;
  MakeCurrent = nullptr;

}

glx_loader glx;
