
#include "loaders/loader_dbus.h"
#include <iostream>

// Put these sanity checks here so that they fire at most once
// (to avoid cluttering the build output).
#if !defined(LIBRARY_LOADER_DBUS_H_DLOPEN) && !defined(LIBRARY_LOADER_DBUS_H_DT_NEEDED)
#error neither LIBRARY_LOADER_DBUS_H_DLOPEN nor LIBRARY_LOADER_DBUS_H_DT_NEEDED defined
#endif
#if defined(LIBRARY_LOADER_DBUS_H_DLOPEN) && defined(LIBRARY_LOADER_DBUS_H_DT_NEEDED)
#error both LIBRARY_LOADER_DBUS_H_DLOPEN and LIBRARY_LOADER_DBUS_H_DT_NEEDED defined
#endif

libdbus_loader::libdbus_loader() : loaded_(false) {
}

libdbus_loader::~libdbus_loader() {
  CleanUp(loaded_);
}

bool libdbus_loader::Load(const std::string& library_name) {
  if (loaded_) {
    return false;
  }

#if defined(LIBRARY_LOADER_DBUS_H_DLOPEN)
  library_ = dlopen(library_name.c_str(), RTLD_LAZY);
  if (!library_) {
    std::cerr << "MANGOHUD: Failed to open " << "" MANGOHUD_ARCH << " " << library_name << ": " << dlerror() << std::endl;
    return false;
  }


  bus_add_match =
      reinterpret_cast<decltype(this->bus_add_match)>(
          dlsym(library_, "dbus_bus_add_match"));
  if (!bus_add_match) {
    CleanUp(true);
    return false;
  }

  bus_get =
      reinterpret_cast<decltype(this->bus_get)>(
          dlsym(library_, "dbus_bus_get"));
  if (!bus_get) {
    CleanUp(true);
    return false;
  }

  bus_get_unique_name =
      reinterpret_cast<decltype(this->bus_get_unique_name)>(
          dlsym(library_, "dbus_bus_get_unique_name"));
  if (!bus_get_unique_name) {
    CleanUp(true);
    return false;
  }

  bus_remove_match =
      reinterpret_cast<decltype(this->bus_remove_match)>(
          dlsym(library_, "dbus_bus_remove_match"));
  if (!bus_remove_match) {
    CleanUp(true);
    return false;
  }

  connection_add_filter =
      reinterpret_cast<decltype(this->connection_add_filter)>(
          dlsym(library_, "dbus_connection_add_filter"));
  if (!connection_add_filter) {
    CleanUp(true);
    return false;
  }

  connection_pop_message =
      reinterpret_cast<decltype(this->connection_pop_message)>(
          dlsym(library_, "dbus_connection_pop_message"));
  if (!connection_pop_message) {
    CleanUp(true);
    return false;
  }

  connection_read_write =
      reinterpret_cast<decltype(this->connection_read_write)>(
          dlsym(library_, "dbus_connection_read_write"));
  if (!connection_read_write) {
    CleanUp(true);
    return false;
  }

  connection_read_write_dispatch =
      reinterpret_cast<decltype(this->connection_read_write)>(
          dlsym(library_, "dbus_connection_read_write_dispatch"));
  if (!connection_read_write_dispatch) {
    CleanUp(true);
    return false;
  }

  connection_remove_filter =
      reinterpret_cast<decltype(this->connection_remove_filter)>(
          dlsym(library_, "dbus_connection_remove_filter"));
  if (!connection_remove_filter) {
    CleanUp(true);
    return false;
  }

  connection_send_with_reply_and_block =
      reinterpret_cast<decltype(this->connection_send_with_reply_and_block)>(
          dlsym(library_, "dbus_connection_send_with_reply_and_block"));
  if (!connection_send_with_reply_and_block) {
    CleanUp(true);
    return false;
  }

  connection_unref =
      reinterpret_cast<decltype(this->connection_unref)>(
          dlsym(library_, "dbus_connection_unref"));
  if (!connection_unref) {
    CleanUp(true);
    return false;
  }

  error_free =
      reinterpret_cast<decltype(this->error_free)>(
          dlsym(library_, "dbus_error_free"));
  if (!error_free) {
    CleanUp(true);
    return false;
  }

  error_init =
      reinterpret_cast<decltype(this->error_init)>(
          dlsym(library_, "dbus_error_init"));
  if (!error_init) {
    CleanUp(true);
    return false;
  }

  error_is_set =
      reinterpret_cast<decltype(this->error_is_set)>(
          dlsym(library_, "dbus_error_is_set"));
  if (!error_is_set) {
    CleanUp(true);
    return false;
  }

  message_append_args =
      reinterpret_cast<decltype(this->message_append_args)>(
          dlsym(library_, "dbus_message_append_args"));
  if (!message_append_args) {
    CleanUp(true);
    return false;
  }

  message_get_interface =
      reinterpret_cast<decltype(this->message_get_interface)>(
          dlsym(library_, "dbus_message_get_interface"));
  if (!message_get_interface) {
    CleanUp(true);
    return false;
  }

  message_get_member =
      reinterpret_cast<decltype(this->message_get_member)>(
          dlsym(library_, "dbus_message_get_member"));
  if (!message_get_member) {
    CleanUp(true);
    return false;
  }

  message_is_signal =
      reinterpret_cast<decltype(this->message_is_signal)>(
          dlsym(library_, "dbus_message_is_signal"));
  if (!message_is_signal) {
    CleanUp(true);
    return false;
  }

  message_iter_get_arg_type =
      reinterpret_cast<decltype(this->message_iter_get_arg_type)>(
          dlsym(library_, "dbus_message_iter_get_arg_type"));
  if (!message_iter_get_arg_type) {
    CleanUp(true);
    return false;
  }

  message_iter_get_basic =
      reinterpret_cast<decltype(this->message_iter_get_basic)>(
          dlsym(library_, "dbus_message_iter_get_basic"));
  if (!message_iter_get_basic) {
    CleanUp(true);
    return false;
  }

  message_iter_init =
      reinterpret_cast<decltype(this->message_iter_init)>(
          dlsym(library_, "dbus_message_iter_init"));
  if (!message_iter_init) {
    CleanUp(true);
    return false;
  }

  message_iter_next =
      reinterpret_cast<decltype(this->message_iter_next)>(
          dlsym(library_, "dbus_message_iter_next"));
  if (!message_iter_next) {
    CleanUp(true);
    return false;
  }

  message_iter_recurse =
      reinterpret_cast<decltype(this->message_iter_recurse)>(
          dlsym(library_, "dbus_message_iter_recurse"));
  if (!message_iter_recurse) {
    CleanUp(true);
    return false;
  }

  message_new_method_call =
      reinterpret_cast<decltype(this->message_new_method_call)>(
          dlsym(library_, "dbus_message_new_method_call"));
  if (!message_new_method_call) {
    CleanUp(true);
    return false;
  }

  message_unref =
      reinterpret_cast<decltype(this->message_unref)>(
          dlsym(library_, "dbus_message_unref"));
  if (!message_unref) {
    CleanUp(true);
    return false;
  }

  move_error =
      reinterpret_cast<decltype(this->move_error)>(
          dlsym(library_, "dbus_move_error"));
  if (!move_error) {
    CleanUp(true);
    return false;
  }

  threads_init_default =
      reinterpret_cast<decltype(this->threads_init_default)>(
          dlsym(library_, "dbus_threads_init_default"));
  if (!threads_init_default) {
    CleanUp(true);
    return false;
  }

  message_get_sender =
      reinterpret_cast<decltype(this->message_get_sender)>(
          dlsym(library_, "dbus_message_get_sender"));
  if (!message_get_sender) {
    CleanUp(true);
    return false;
  }

#endif

#if defined(LIBRARY_LOADER_DBUS_H_DT_NEEDED)
  bus_add_match = &::dbus_bus_add_match;
  bus_get = &::dbus_bus_get;
  bus_get_unique_name = &::dbus_bus_get_unique_name;
  bus_remove_match = &::dbus_bus_remove_match;
  connection_pop_message = &::dbus_connection_pop_message;
  connection_read_write = &::dbus_connection_read_write;
  connection_send_with_reply_and_block = &::dbus_connection_send_with_reply_and_block;
  connection_unref = &::dbus_connection_unref;
  error_free = &::dbus_error_free;
  error_init = &::dbus_error_init;
  error_is_set = &::dbus_error_is_set;
  message_append_args = &::dbus_message_append_args;
  message_is_signal = &::dbus_message_is_signal;
  message_iter_get_arg_type = &::dbus_message_iter_get_arg_type;
  message_iter_get_basic = &::dbus_message_iter_get_basic;
  message_iter_init = &::dbus_message_iter_init;
  message_iter_next = &::dbus_message_iter_next;
  message_iter_recurse = &::dbus_message_iter_recurse;
  message_new_method_call = &::dbus_message_new_method_call;
  message_unref = &::dbus_message_unref;
  move_error = &::dbus_move_error;
  threads_init_default = &::dbus_threads_init_default;

#endif

  loaded_ = true;
  return true;
}

void libdbus_loader::CleanUp(bool unload) {
#if defined(LIBRARY_LOADER_DBUS_H_DLOPEN)
  if (unload) {
    dlclose(library_);
    library_ = NULL;
  }
#endif
  loaded_ = false;
  bus_add_match = NULL;
  bus_get = NULL;
  bus_get_unique_name = NULL;
  bus_remove_match = NULL;
  connection_pop_message = NULL;
  connection_read_write = NULL;
  connection_send_with_reply_and_block = NULL;
  connection_unref = NULL;
  error_free = NULL;
  error_init = NULL;
  error_is_set = NULL;
  message_append_args = NULL;
  message_is_signal = NULL;
  message_iter_get_arg_type = NULL;
  message_iter_get_basic = NULL;
  message_iter_init = NULL;
  message_iter_next = NULL;
  message_iter_recurse = NULL;
  message_new_method_call = NULL;
  message_unref = NULL;
  move_error = NULL;
  threads_init_default = NULL;

}
