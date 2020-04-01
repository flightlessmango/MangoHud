#include "loader_x11.h"
#include "real_dlsym.h"

libx11_loader::libx11_loader() : loaded_(false) {
}

libx11_loader::~libx11_loader() {
  CleanUp(loaded_);
}

bool libx11_loader::Load(const std::string& library_name) {
  if (loaded_) {
    return false;
  }

  library_ = real_dlopen(library_name.c_str(), RTLD_LAZY);
  if (!library_)
    return false;


  XOpenDisplay =
      reinterpret_cast<decltype(this->XOpenDisplay)>(
          dlsym(library_, "XOpenDisplay"));
  if (!XOpenDisplay) {
    CleanUp(true);
    return false;
  }

  XCloseDisplay =
      reinterpret_cast<decltype(this->XCloseDisplay)>(
          dlsym(library_, "XCloseDisplay"));
  if (!XCloseDisplay) {
    CleanUp(true);
    return false;
  }

  XQueryKeymap =
      reinterpret_cast<decltype(this->XQueryKeymap)>(
          dlsym(library_, "XQueryKeymap"));
  if (!XQueryKeymap) {
    CleanUp(true);
    return false;
  }

  XKeysymToKeycode =
      reinterpret_cast<decltype(this->XKeysymToKeycode)>(
          dlsym(library_, "XKeysymToKeycode"));
  if (!XKeysymToKeycode) {
    CleanUp(true);
    return false;
  }

  XStringToKeysym =
      reinterpret_cast<decltype(this->XStringToKeysym)>(
          dlsym(library_, "XStringToKeysym"));
  if (!XStringToKeysym) {
    CleanUp(true);
    return false;
  }

  XGetGeometry =
      reinterpret_cast<decltype(this->XGetGeometry)>(
          dlsym(library_, "XGetGeometry"));
  if (!XGetGeometry) {
    CleanUp(true);
    return false;
  }

  loaded_ = true;
  return true;
}

void libx11_loader::CleanUp(bool unload) {
  if (unload) {
    dlclose(library_);
    library_ = NULL;
  }

  loaded_ = false;
  XOpenDisplay = NULL;
  XCloseDisplay = NULL;
  XQueryKeymap = NULL;
  XKeysymToKeycode = NULL;
  XStringToKeysym = NULL;
  XGetGeometry = NULL;

}

std::shared_ptr<libx11_loader> g_x11(new libx11_loader("libX11.so.6"));
