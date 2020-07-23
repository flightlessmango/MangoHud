
#ifndef LIBRARY_LOADER_DBUS_H
#define LIBRARY_LOADER_DBUS_H

#include <dbus/dbus.h>
#define LIBRARY_LOADER_DBUS_H_DLOPEN


#include <string>
#include <dlfcn.h>

class libdbus_loader {
 public:
  libdbus_loader();
  libdbus_loader(const std::string& library_name) : libdbus_loader() {
    Load(library_name);
  }
  ~libdbus_loader();

  bool Load(const std::string& library_name);
  bool IsLoaded() { return loaded_; }

  decltype(&::dbus_bus_add_match) bus_add_match;
  decltype(&::dbus_bus_get) bus_get;
  decltype(&::dbus_bus_get_unique_name) bus_get_unique_name;
  decltype(&::dbus_bus_remove_match) bus_remove_match;
  decltype(&::dbus_connection_add_filter) connection_add_filter;
  decltype(&::dbus_connection_pop_message) connection_pop_message;
  decltype(&::dbus_connection_read_write) connection_read_write;
  decltype(&::dbus_connection_read_write_dispatch) connection_read_write_dispatch;
  decltype(&::dbus_connection_remove_filter) connection_remove_filter;
  decltype(&::dbus_connection_send_with_reply_and_block) connection_send_with_reply_and_block;
  decltype(&::dbus_connection_unref) connection_unref;
  decltype(&::dbus_error_free) error_free;
  decltype(&::dbus_error_init) error_init;
  decltype(&::dbus_error_is_set) error_is_set;
  decltype(&::dbus_message_append_args) message_append_args;
  decltype(&::dbus_message_get_sender) message_get_sender;
  decltype(&::dbus_message_get_interface) message_get_interface;
  decltype(&::dbus_message_get_member) message_get_member;
  decltype(&::dbus_message_is_signal) message_is_signal;
  decltype(&::dbus_message_iter_get_arg_type) message_iter_get_arg_type;
  decltype(&::dbus_message_iter_get_basic) message_iter_get_basic;
  decltype(&::dbus_message_iter_init) message_iter_init;
  decltype(&::dbus_message_iter_next) message_iter_next;
  decltype(&::dbus_message_iter_recurse) message_iter_recurse;
  decltype(&::dbus_message_new_method_call) message_new_method_call;
  decltype(&::dbus_message_unref) message_unref;
  decltype(&::dbus_move_error) move_error;
  decltype(&::dbus_threads_init_default) threads_init_default;


 private:
  void CleanUp(bool unload);

#if defined(LIBRARY_LOADER_DBUS_H_DLOPEN)
  void* library_;
#endif

  bool loaded_;

  // Disallow copy constructor and assignment operator.
  libdbus_loader(const libdbus_loader&);
  void operator=(const libdbus_loader&);
};

#endif  // LIBRARY_LOADER_DBUS_H
