#include "gl/real_dlsym.h"
#include "loaders/loader_gl.h"

gl_loader::gl_loader() : loaded_(false) {
}

gl_loader::~gl_loader() {
  CleanUp(loaded_);
}

bool gl_loader::Load(bool egl_only) {
  if (loaded_) {
    return true;
  }

  eglSwapBuffers =
      reinterpret_cast<decltype(this->eglSwapBuffers)>(
          real_dlsym(RTLD_NEXT, "eglSwapBuffers"));

  if (egl_only) {
    loaded_ = true;
    return true;
  }

  glXGetProcAddress =
      reinterpret_cast<decltype(this->glXGetProcAddress)>(
          real_dlsym(RTLD_NEXT, "glXGetProcAddress"));

  glXGetProcAddressARB =
      reinterpret_cast<decltype(this->glXGetProcAddressARB)>(
          real_dlsym(RTLD_NEXT, "glXGetProcAddressARB"));

  if (!glXGetProcAddress) {
    CleanUp(true);
    return false;
  }

  glXCreateContext =
      reinterpret_cast<decltype(this->glXCreateContext)>(
          glXGetProcAddress((const unsigned char *)"glXCreateContext"));
  if (!glXCreateContext) {
    CleanUp(true);
    return false;
  }

  glXDestroyContext =
      reinterpret_cast<decltype(this->glXDestroyContext)>(
          glXGetProcAddress((const unsigned char *)"glXDestroyContext"));
  if (!glXDestroyContext) {
    CleanUp(true);
    return false;
  }

  glXSwapBuffers =
      reinterpret_cast<decltype(this->glXSwapBuffers)>(
          glXGetProcAddress((const unsigned char *)"glXSwapBuffers"));
  if (!glXSwapBuffers) {
    CleanUp(true);
    return false;
  }

  glXSwapIntervalEXT =
      reinterpret_cast<decltype(this->glXSwapIntervalEXT)>(
          glXGetProcAddress((const unsigned char *)"glXSwapIntervalEXT"));

  glXSwapIntervalSGI =
      reinterpret_cast<decltype(this->glXSwapIntervalSGI)>(
          glXGetProcAddress((const unsigned char *)"glXSwapIntervalSGI"));

  glXSwapIntervalMESA =
      reinterpret_cast<decltype(this->glXSwapIntervalMESA)>(
          glXGetProcAddress((const unsigned char *)"glXSwapIntervalMESA"));

  glXMakeCurrent =
      reinterpret_cast<decltype(this->glXMakeCurrent)>(
          glXGetProcAddress((const unsigned char *)"glXMakeCurrent"));
  if (!glXMakeCurrent) {
    CleanUp(true);
    return false;
  }

  glClipControl =
      reinterpret_cast<decltype(this->glClipControl)>(
          glXGetProcAddress((const unsigned char *)"glClipControl"));

  loaded_ = true;
  return true;
}

void gl_loader::CleanUp(bool unload) {
  loaded_ = false;
  glXGetProcAddress = nullptr;
  glXGetProcAddressARB = nullptr;
  glXCreateContext = nullptr;
  glXDestroyContext = nullptr;
  glXSwapBuffers = nullptr;
  glXSwapIntervalEXT = nullptr;
  glXSwapIntervalSGI = nullptr;
  glXSwapIntervalMESA = nullptr;
  glXMakeCurrent = nullptr;

}
