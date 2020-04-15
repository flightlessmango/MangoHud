
#include "loader_libinput.h"

// Put these sanity checks here so that they fire at most once
// (to avoid cluttering the build output).
#if !defined(LIBRARY_LOADER_LIBINPUT_H_DLOPEN) && !defined(LIBRARY_LOADER_LIBINPUT_H_DT_NEEDED)
#error neither LIBRARY_LOADER_LIBINPUT_H_DLOPEN nor LIBRARY_LOADER_LIBINPUT_H_DT_NEEDED defined
#endif
#if defined(LIBRARY_LOADER_LIBINPUT_H_DLOPEN) && defined(LIBRARY_LOADER_LIBINPUT_H_DT_NEEDED)
#error both LIBRARY_LOADER_LIBINPUT_H_DLOPEN and LIBRARY_LOADER_LIBINPUT_H_DT_NEEDED defined
#endif

libinput_loader::libinput_loader() : loaded_(false) {
}

libinput_loader::~libinput_loader() {
  CleanUp(loaded_);
}

bool libinput_loader::Load(const std::string& library_name) {
  if (loaded_) {
    return false;
  }

#if defined(LIBRARY_LOADER_LIBINPUT_H_DLOPEN)
  library_ = dlopen(library_name.c_str(), RTLD_LAZY);
  if (!library_)
    return false;


  udev_create_context =
      reinterpret_cast<decltype(this->udev_create_context)>(
          dlsym(library_, "libinput_udev_create_context"));
  if (!udev_create_context) {
    CleanUp(true);
    return false;
  }

  udev_assign_seat =
      reinterpret_cast<decltype(this->udev_assign_seat)>(
          dlsym(library_, "libinput_udev_assign_seat"));
  if (!udev_assign_seat) {
    CleanUp(true);
    return false;
  }

  dispatch =
      reinterpret_cast<decltype(this->dispatch)>(
          dlsym(library_, "libinput_dispatch"));
  if (!dispatch) {
    CleanUp(true);
    return false;
  }

  get_event =
      reinterpret_cast<decltype(this->get_event)>(
          dlsym(library_, "libinput_get_event"));
  if (!get_event) {
    CleanUp(true);
    return false;
  }

  event_get_type =
      reinterpret_cast<decltype(this->event_get_type)>(
          dlsym(library_, "libinput_event_get_type"));
  if (!event_get_type) {
    CleanUp(true);
    return false;
  }

  device_get_name =
      reinterpret_cast<decltype(this->device_get_name)>(
          dlsym(library_, "libinput_device_get_name"));
  if (!device_get_name) {
    CleanUp(true);
    return false;
  }

  event_get_keyboard_event =
      reinterpret_cast<decltype(this->event_get_keyboard_event)>(
          dlsym(library_, "libinput_event_get_keyboard_event"));
  if (!event_get_keyboard_event) {
    CleanUp(true);
    return false;
  }

  event_keyboard_get_key =
      reinterpret_cast<decltype(this->event_keyboard_get_key)>(
          dlsym(library_, "libinput_event_keyboard_get_key"));
  if (!event_keyboard_get_key) {
    CleanUp(true);
    return false;
  }

  event_keyboard_get_key_state =
      reinterpret_cast<decltype(this->event_keyboard_get_key_state)>(
          dlsym(library_, "libinput_event_keyboard_get_key_state"));
  if (!event_keyboard_get_key_state) {
    CleanUp(true);
    return false;
  }

  event_destroy =
      reinterpret_cast<decltype(this->event_destroy)>(
          dlsym(library_, "libinput_event_destroy"));
  if (!event_destroy) {
    CleanUp(true);
    return false;
  }

  unref =
      reinterpret_cast<decltype(this->unref)>(
          dlsym(library_, "libinput_unref"));
  if (!unref) {
    CleanUp(true);
    return false;
  }

#endif

#if defined(LIBRARY_LOADER_LIBINPUT_H_DT_NEEDED)
  libinput_udev_create_context = &::libinput_udev_create_context;
  libinput_udev_assign_seat = &::libinput_udev_assign_seat;
  libinput_dispatch = &::libinput_dispatch;
  libinput_get_event = &::libinput_get_event;
  libinput_event_get_type = &::libinput_event_get_type;
  libinput_device_get_name = &::libinput_device_get_name;
  libinput_event_get_keyboard_event = &::libinput_event_get_keyboard_event;
  libinput_event_keyboard_get_key = &::libinput_event_keyboard_get_key;
  libinput_event_keyboard_get_key_state = &::libinput_event_keyboard_get_key_state;
  libinput_event_destroy = &::libinput_event_destroy;
  libinput_dispatch = &::libinput_dispatch;
  libinput_unref = &::libinput_unref;

#endif

  loaded_ = true;
  return true;
}

void libinput_loader::CleanUp(bool unload) {
#if defined(LIBRARY_LOADER_LIBINPUT_H_DLOPEN)
  if (unload) {
    dlclose(library_);
    library_ = NULL;
  }
#endif
  loaded_ = false;
  udev_create_context = NULL;
  udev_assign_seat = NULL;
  dispatch = NULL;
  get_event = NULL;
  event_get_type = NULL;
  device_get_name = NULL;
  event_get_keyboard_event = NULL;
  event_keyboard_get_key = NULL;
  event_keyboard_get_key_state = NULL;
  event_destroy = NULL;
  dispatch = NULL;
  unref = NULL;

}
