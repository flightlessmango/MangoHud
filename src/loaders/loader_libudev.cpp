
#include "loader_libudev.h"

// Put these sanity checks here so that they fire at most once
// (to avoid cluttering the build output).
#if !defined(LIBRARY_LOADER_LIBUDEV_H_DLOPEN) && !defined(LIBRARY_LOADER_LIBUDEV_H_DT_NEEDED)
#error neither LIBRARY_LOADER_LIBUDEV_H_DLOPEN nor LIBRARY_LOADER_LIBUDEV_H_DT_NEEDED defined
#endif
#if defined(LIBRARY_LOADER_LIBUDEV_H_DLOPEN) && defined(LIBRARY_LOADER_LIBUDEV_H_DT_NEEDED)
#error both LIBRARY_LOADER_LIBUDEV_H_DLOPEN and LIBRARY_LOADER_LIBUDEV_H_DT_NEEDED defined
#endif

libudev_loader::libudev_loader() : loaded_(false) {
}

libudev_loader::~libudev_loader() {
  CleanUp(loaded_);
}

bool libudev_loader::Load(const std::string& library_name) {
  if (loaded_) {
    return false;
  }

#if defined(LIBRARY_LOADER_LIBUDEV_H_DLOPEN)
  library_ = dlopen(library_name.c_str(), RTLD_LAZY);
  if (!library_)
    return false;


  udev_new =
      reinterpret_cast<decltype(this->udev_new)>(
          dlsym(library_, "udev_new"));
  if (!udev_new) {
    CleanUp(true);
    return false;
  }

  udev_unref =
      reinterpret_cast<decltype(this->udev_unref)>(
          dlsym(library_, "udev_unref"));
  if (!udev_unref) {
    CleanUp(true);
    return false;
  }

#endif

#if defined(LIBRARY_LOADER_LIBUDEV_H_DT_NEEDED)
  udev_new = &::udev_new;
  udev_unref = &::udev_unref;

#endif

  loaded_ = true;
  return true;
}

void libudev_loader::CleanUp(bool unload) {
#if defined(LIBRARY_LOADER_LIBUDEV_H_DLOPEN)
  if (unload) {
    dlclose(library_);
    library_ = NULL;
  }
#endif
  loaded_ = false;
  udev_new = NULL;
  udev_unref = NULL;

}
