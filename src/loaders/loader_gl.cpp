#include "gl/real_dlsym.h"
#include "loaders/loader_gl.h"

gl_loader::gl_loader() : loaded_(false) {
}

gl_loader::~gl_loader() {
  CleanUp(loaded_);
}

bool gl_loader::Load(void *handle, bool egl_only) {
  if (loaded_) {
    return true;
  }

  if (!handle)
    handle = RTLD_NEXT;

  eglSwapBuffers =
      reinterpret_cast<decltype(this->eglSwapBuffers)>(
          real_dlsym(handle, "eglSwapBuffers"));

  if (egl_only) {
    loaded_ = true;
    return true;
  }

  glXGetProcAddress =
      reinterpret_cast<decltype(this->glXGetProcAddress)>(
          real_dlsym(handle, "glXGetProcAddress"));

  glXGetProcAddressARB =
      reinterpret_cast<decltype(this->glXGetProcAddressARB)>(
          real_dlsym(handle, "glXGetProcAddressARB"));

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

  glXGetCurrentContext =
      reinterpret_cast<decltype(this->glXGetCurrentContext)>(
          glXGetProcAddress((const unsigned char *)"glXGetCurrentContext"));
  if (!glXGetCurrentContext) {
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

  glXGetSwapIntervalMESA =
      reinterpret_cast<decltype(this->glXGetSwapIntervalMESA)>(
          glXGetProcAddress((const unsigned char *)"glXGetSwapIntervalMESA"));

  glXQueryDrawable =
      reinterpret_cast<decltype(this->glXQueryDrawable)>(
          glXGetProcAddress((const unsigned char *)"glXQueryDrawable"));

  glXMakeCurrent =
      reinterpret_cast<decltype(this->glXMakeCurrent)>(
          glXGetProcAddress((const unsigned char *)"glXMakeCurrent"));
  if (!glXMakeCurrent) {
    CleanUp(true);
    return false;
  }

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
  glXQueryDrawable = nullptr;
  glXMakeCurrent = nullptr;

}
