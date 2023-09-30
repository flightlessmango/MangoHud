#ifndef LIBRARY_LOADER_NVCTRL_H
#define LIBRARY_LOADER_NVCTRL_H
#define Bool bool
#include <X11/Xlib.h>
#include "NVCtrl/NVCtrlLib.h"
#define LIBRARY_LOADER_NVCTRL_H_DLOPEN

#include <string>
#include <dlfcn.h>

class libnvctrl_loader {
 public:
  libnvctrl_loader();
  libnvctrl_loader(const std::string& library_name) : libnvctrl_loader() {
    Load(library_name);
  }
  ~libnvctrl_loader();

  bool Load(const std::string& library_name);
  bool IsLoaded() { return loaded_; }

  decltype(&::XNVCTRLIsNvScreen) XNVCTRLIsNvScreen;
  decltype(&::XNVCTRLQueryVersion) XNVCTRLQueryVersion;
  decltype(&::XNVCTRLQueryAttribute) XNVCTRLQueryAttribute;
  decltype(&::XNVCTRLQueryTargetStringAttribute) XNVCTRLQueryTargetStringAttribute;
  decltype(&::XNVCTRLQueryTargetAttribute64) XNVCTRLQueryTargetAttribute64;
  decltype(&::XNVCTRLQueryTargetCount) XNVCTRLQueryTargetCount;

 private:
  void CleanUp(bool unload);

#if defined(LIBRARY_LOADER_NVCTRL_H_DLOPEN)
  void* library_;
#endif

  bool loaded_;

  // Disallow copy constructor and assignment operator.
  libnvctrl_loader(const libnvctrl_loader&);
  void operator=(const libnvctrl_loader&);
};

libnvctrl_loader& get_libnvctrl_loader();
#endif  // LIBRARY_LOADER_NVCTRL_H
