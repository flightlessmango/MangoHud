
#ifndef LIBRARY_LOADER_LIBINPUT_H
#define LIBRARY_LOADER_LIBINPUT_H

#include <libudev.h>
#include <libinput.h>
#define LIBRARY_LOADER_LIBINPUT_H_DLOPEN


#include <string>
#include <dlfcn.h>

class libinput_loader {
 public:
  libinput_loader();
  libinput_loader(const std::string& library_name) : libinput_loader() {
    Load(library_name);
  }
  ~libinput_loader();

  bool Load(const std::string& library_name);
  bool IsLoaded() { return loaded_; }

  decltype(&::libinput_udev_create_context) udev_create_context;
  decltype(&::libinput_udev_assign_seat) udev_assign_seat;
  decltype(&::libinput_dispatch) dispatch;
  decltype(&::libinput_get_event) get_event;
  decltype(&::libinput_event_get_type) event_get_type;
  decltype(&::libinput_device_get_name) device_get_name;
  decltype(&::libinput_event_get_keyboard_event) event_get_keyboard_event;
  decltype(&::libinput_event_keyboard_get_key) event_keyboard_get_key;
  decltype(&::libinput_event_keyboard_get_key_state) event_keyboard_get_key_state;
  decltype(&::libinput_event_destroy) event_destroy;
  decltype(&::libinput_unref) unref;

 private:
  void CleanUp(bool unload);

#if defined(LIBRARY_LOADER_LIBINPUT_H_DLOPEN)
  void* library_;
#endif

  bool loaded_;

  // Disallow copy constructor and assignment operator.
  libinput_loader(const libinput_loader&);
  void operator=(const libinput_loader&);
};

#endif  // LIBRARY_LOADER_LIBINPUT_H
