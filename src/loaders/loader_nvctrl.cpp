// This is generated file. Do not modify directly.
// Path to the code generator: /home/crz/git/MangoHud/generate_library_loader.py .

#include "loader_nvctrl.h"

// Put these sanity checks here so that they fire at most once
// (to avoid cluttering the build output).
#if !defined(LIBRARY_LOADER_NVCTRL_H_DLOPEN) && !defined(LIBRARY_LOADER_NVCTRL_H_DT_NEEDED)
#error neither LIBRARY_LOADER_NVCTRL_H_DLOPEN nor LIBRARY_LOADER_NVCTRL_H_DT_NEEDED defined
#endif
#if defined(LIBRARY_LOADER_NVCTRL_H_DLOPEN) && defined(LIBRARY_LOADER_NVCTRL_H_DT_NEEDED)
#error both LIBRARY_LOADER_NVCTRL_H_DLOPEN and LIBRARY_LOADER_NVCTRL_H_DT_NEEDED defined
#endif

libnvctrl_loader::libnvctrl_loader() : loaded_(false) {
}

libnvctrl_loader::~libnvctrl_loader() {
  CleanUp(loaded_);
}

bool libnvctrl_loader::Load(const std::string& library_name) {
  if (loaded_) {
    return false;
  }

#if defined(LIBRARY_LOADER_NVCTRL_H_DLOPEN)
  library_ = dlopen(library_name.c_str(), RTLD_LAZY);
  if (!library_)
    return false;

  XNVCTRLIsNvScreen =
      reinterpret_cast<decltype(this->XNVCTRLIsNvScreen)>(
          dlsym(library_, "XNVCTRLIsNvScreen"));
  if (!XNVCTRLIsNvScreen) {
    CleanUp(true);
    return false;
  }

  XNVCTRLQueryVersion =
      reinterpret_cast<decltype(this->XNVCTRLQueryVersion)>(
          dlsym(library_, "XNVCTRLQueryVersion"));
  if (!XNVCTRLQueryVersion) {
    CleanUp(true);
    return false;
  }

  XNVCTRLQueryAttribute =
      reinterpret_cast<decltype(this->XNVCTRLQueryAttribute)>(
          dlsym(library_, "XNVCTRLQueryAttribute"));
  if (!XNVCTRLQueryAttribute) {
    CleanUp(true);
    return false;
  }

  XNVCTRLQueryTargetStringAttribute =
      reinterpret_cast<decltype(this->XNVCTRLQueryTargetStringAttribute)>(
          dlsym(library_, "XNVCTRLQueryTargetStringAttribute"));
  if (!XNVCTRLQueryTargetStringAttribute) {
    CleanUp(true);
    return false;
  }
  
  XNVCTRLQueryTargetAttribute64 =
      reinterpret_cast<decltype(this->XNVCTRLQueryTargetAttribute64)>(
          dlsym(library_, "XNVCTRLQueryTargetAttribute64"));
  if (!XNVCTRLQueryTargetAttribute64) {
    CleanUp(true);
    return false;
  }

#endif

#if defined(LIBRARY_LOADER_NVCTRL_H_DT_NEEDED)
  XNVCTRLQueryVersion = &::XNVCTRLQueryVersion;
  XNVCTRLQueryAttribute = &::XNVCTRLQueryAttribute;

#endif

  loaded_ = true;
  return true;
}

void libnvctrl_loader::CleanUp(bool unload) {
#if defined(LIBRARY_LOADER_NVCTRL_H_DLOPEN)
  if (unload) {
    dlclose(library_);
    library_ = NULL;
  }
#endif
  loaded_ = false;
  XNVCTRLQueryVersion = NULL;
  XNVCTRLQueryAttribute = NULL;

}
