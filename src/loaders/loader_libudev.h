
#ifndef LIBRARY_LOADER_LIBUDEV_H
#define LIBRARY_LOADER_LIBUDEV_H

#include <libudev.h>
#define LIBRARY_LOADER_LIBUDEV_H_DLOPEN


#include <string>
#include <dlfcn.h>

class libudev_loader {
 public:
  libudev_loader();
  libudev_loader(const std::string& library_name) : libudev_loader() {
    Load(library_name);
  }
  ~libudev_loader();

  bool Load(const std::string& library_name);
  bool IsLoaded() { return loaded_; }

  decltype(&::udev_new) udev_new;
  decltype(&::udev_unref) udev_unref;


 private:
  void CleanUp(bool unload);

#if defined(LIBRARY_LOADER_LIBUDEV_H_DLOPEN)
  void* library_;
#endif

  bool loaded_;

  // Disallow copy constructor and assignment operator.
  libudev_loader(const libudev_loader&);
  void operator=(const libudev_loader&);
};

#endif  // LIBRARY_LOADER_LIBUDEV_H
