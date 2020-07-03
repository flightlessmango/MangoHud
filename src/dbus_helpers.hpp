#pragma once
#ifndef MANGOHUD_DBUS_HELPERS
#define MANGOHUD_DBUS_HELPERS

#include <vector>

#include "loaders/loader_dbus.h"

namespace DBus_helpers {
namespace detail {
// clang-format off
template<class T> struct dbus_type_traits{};
template<> struct dbus_type_traits<bool>        { const int value = DBUS_TYPE_BOOLEAN; const bool is_fixed = true; };
template<> struct dbus_type_traits<uint8_t>     { const int value = DBUS_TYPE_BYTE;    const bool is_fixed = true; };
template<> struct dbus_type_traits<uint16_t>    { const int value = DBUS_TYPE_UINT16;  const bool is_fixed = true; };
template<> struct dbus_type_traits<uint32_t>    { const int value = DBUS_TYPE_UINT32;  const bool is_fixed = true; };
template<> struct dbus_type_traits<uint64_t>    { const int value = DBUS_TYPE_UINT64;  const bool is_fixed = true; };
template<> struct dbus_type_traits<int16_t>     { const int value = DBUS_TYPE_INT16;   const bool is_fixed = true; };
template<> struct dbus_type_traits<int32_t>     { const int value = DBUS_TYPE_INT32;   const bool is_fixed = true; };
template<> struct dbus_type_traits<int64_t>     { const int value = DBUS_TYPE_INT64;   const bool is_fixed = true; };
template<> struct dbus_type_traits<double>      { const int value = DBUS_TYPE_DOUBLE;  const bool is_fixed = true; };
template<> struct dbus_type_traits<const char*> { const int value = DBUS_TYPE_STRING;  const bool is_fixed = false; };
// clang-format on

template <class T>
const int dbus_type_identifier = dbus_type_traits<T>().value;

template <class T>
const bool is_fixed = dbus_type_traits<T>().is_fiexd;
}  // namespace detail

class DBusMessageIter_wrap {
   public:
    DBusMessageIter_wrap(DBusMessage* msg, libdbus_loader* loader);
    DBusMessageIter_wrap(DBusMessageIter iter, libdbus_loader* loader);

    //  Type accessors
    int type() const noexcept { return m_type; }
    bool is_unsigned() const noexcept;
    bool is_signed() const noexcept;
    bool is_string() const noexcept;
    bool is_double() const noexcept;
    bool is_primitive() const noexcept;
    bool is_array() const noexcept;
    operator bool() const noexcept { return type() != DBUS_TYPE_INVALID; }

    //  Value accessors
    //  Primitives
    template <class T>
    auto get_primitive() -> T;
    auto get_unsigned() -> uint64_t;
    auto get_signed() -> int64_t;
    auto get_stringified() -> std::string;
    //  Composites
    auto get_array_iter() -> DBusMessageIter_wrap;
    auto get_dict_entry_iter() -> DBusMessageIter_wrap;

    //  Looping
    template <class Callable>
    void array_for_each(Callable);
    template <class Callable>
    void array_for_each_stringify(Callable);
    template <class T, class Callable>
    void array_for_each_value(Callable);

    template <class Callable>
    void string_map_for_each(Callable);
    template <class Callable>
    void string_multimap_for_each_stringify(Callable);

    auto next() -> DBusMessageIter_wrap&;

   private:
    DBusMessageIter resolve_variants() {
        auto iter = m_Iter;
        auto field_type = m_DBus->message_iter_get_arg_type(&m_Iter);
        while (field_type == DBUS_TYPE_VARIANT) {
            m_DBus->message_iter_recurse(&iter, &iter);
            field_type = m_DBus->message_iter_get_arg_type(&iter);
        }
        return iter;
    }

    DBusMessageIter m_Iter;
    DBusMessageIter m_resolved_iter;
    int m_type;
    libdbus_loader* m_DBus;
};

DBusMessageIter_wrap::DBusMessageIter_wrap(DBusMessage* msg,
                                           libdbus_loader* loader) {
    m_DBus = loader;
    if (msg) {
        m_DBus->message_iter_init(msg, &m_Iter);
        m_resolved_iter = resolve_variants();
        m_type = m_DBus->message_iter_get_arg_type(&m_resolved_iter);
    } else {
        m_type = DBUS_TYPE_INVALID;
    }
}

DBusMessageIter_wrap::DBusMessageIter_wrap(DBusMessageIter iter,
                                           libdbus_loader* loader)
    : m_Iter(iter), m_DBus(loader) {
    m_resolved_iter = resolve_variants();
    m_type = m_DBus->message_iter_get_arg_type(&m_resolved_iter);
}

bool DBusMessageIter_wrap::is_unsigned() const noexcept {
    return ((type() == DBUS_TYPE_BYTE) || (type() == DBUS_TYPE_INT16) ||
            (type() == DBUS_TYPE_INT32) || (type() == DBUS_TYPE_INT64));
}

bool DBusMessageIter_wrap::is_signed() const noexcept {
    return ((type() == DBUS_TYPE_INT16) || (type() == DBUS_TYPE_INT32) ||
            (type() == DBUS_TYPE_INT64));
}

bool DBusMessageIter_wrap::is_string() const noexcept {
    return (type() == DBUS_TYPE_STRING);
}

bool DBusMessageIter_wrap::is_double() const noexcept {
    return (type() == DBUS_TYPE_DOUBLE);
}

bool DBusMessageIter_wrap::is_primitive() const noexcept {
    return (is_double() || is_signed() || is_unsigned() || is_string());
}

bool DBusMessageIter_wrap::is_array() const noexcept {
    return (type() == DBUS_TYPE_ARRAY);
}

template <class T>
auto DBusMessageIter_wrap::get_primitive() -> T {
    auto requested_type = detail::dbus_type_identifier<T>;
    if (requested_type != type()) {
        std::cerr << "Type mismatch: '" << ((char)requested_type) << "' vs '"
                  << (char)type() << "'\n";
#ifndef NDEBUG
        exit(-1);
#else
        return T();
#endif
    }

    T ret;
    m_DBus->message_iter_get_basic(&m_resolved_iter, &ret);
    return ret;
}

template <>
auto DBusMessageIter_wrap::get_primitive<std::string>() -> std::string {
    return std::string(get_primitive<const char*>());
}

uint64_t DBusMessageIter_wrap::get_unsigned() {
    auto t = type();
    switch (t) {
        case DBUS_TYPE_BYTE:
            return get_primitive<uint8_t>();
        case DBUS_TYPE_UINT16:
            return get_primitive<uint16_t>();
        case DBUS_TYPE_UINT32:
            return get_primitive<uint32_t>();
        case DBUS_TYPE_UINT64:
            return get_primitive<uint64_t>();
        default:
            return 0;
    }
}

int64_t DBusMessageIter_wrap::get_signed() {
    auto t = type();
    switch (t) {
        case DBUS_TYPE_INT16:
            return get_primitive<int16_t>();
        case DBUS_TYPE_INT32:
            return get_primitive<int32_t>();
        case DBUS_TYPE_INT64:
            return get_primitive<int64_t>();
        default:
            return 0;
    }
}

auto DBusMessageIter_wrap::get_stringified() -> std::string {
    if (is_string()) return get_primitive<std::string>();
    if (is_unsigned()) return std::to_string(get_unsigned());
    if (is_signed()) return std::to_string(get_signed());
    if (is_double()) return std::to_string(get_primitive<double>());
    std::cerr << "stringify failed\n";
    return std::string();
}

auto DBusMessageIter_wrap::get_array_iter() -> DBusMessageIter_wrap {
    if (not is_array()) {
        std::cerr << "Not an array; " << (char)type() << "\n";
        return DBusMessageIter_wrap(DBusMessageIter{}, m_DBus);
    }

    DBusMessageIter ret;
    m_DBus->message_iter_recurse(&m_resolved_iter, &ret);
    return DBusMessageIter_wrap(ret, m_DBus);
}

auto DBusMessageIter_wrap::get_dict_entry_iter() -> DBusMessageIter_wrap {
    if (type() != DBUS_TYPE_DICT_ENTRY) {
        std::cerr << "Not a dict entry" << (char)type() << "\n";
        return DBusMessageIter_wrap(DBusMessageIter{}, m_DBus);
    }

    DBusMessageIter ret;
    m_DBus->message_iter_recurse(&m_resolved_iter, &ret);
    return DBusMessageIter_wrap(ret, m_DBus);
}

template <class T, class Callable>
void DBusMessageIter_wrap::array_for_each_value(Callable action) {
    auto iter = get_array_iter();
    for (; iter; iter.next()) {
        action(iter.get_primitive<T>());
    }
}

template <class Callable>
void DBusMessageIter_wrap::array_for_each(Callable action) {
    auto iter = get_array_iter();
    for (; iter; iter.next()) {
        action(iter);
    }
}

template <class Callable>
void DBusMessageIter_wrap::array_for_each_stringify(Callable action) {
    auto iter = get_array_iter();
    for (; iter; iter.next()) {
        action(iter.get_stringified());
    }
}

template <class T>
void DBusMessageIter_wrap::string_map_for_each(T action) {
    auto iter = get_array_iter();
    for (; iter; iter.next()) {
        auto it = iter.get_dict_entry_iter();
        auto key = it.get_primitive<std::string>();

        it.next();
        action(key, it);
    }
}

template <class T>
void DBusMessageIter_wrap::string_multimap_for_each_stringify(T action) {
    string_map_for_each([&action](const std::string& key, DBusMessageIter_wrap it) {
        if (it.is_array()) {
            it.array_for_each_stringify(
                [&](const std::string& val) { action(key, val); });
        } else if (it.is_primitive()) {
            action(key, it.get_stringified());
        }
    });
}

auto DBusMessageIter_wrap::next() -> DBusMessageIter_wrap& {
    if (not *this) return *this;
    m_DBus->message_iter_next(&m_Iter);
    // Resolve any variants
    m_resolved_iter = resolve_variants();
    m_type = m_DBus->message_iter_get_arg_type(&m_resolved_iter);
    return *this;
}


class DBusMessage_wrap {
   public:
    DBusMessage_wrap(DBusMessage* msg, libdbus_loader* ldr, bool owning = false)
        : m_owning(owning), m_msg(msg), m_DBus(ldr) {}

    ~DBusMessage_wrap() { free_if_owning(); }

    DBusMessage_wrap(const DBusMessage_wrap&) = delete;
    DBusMessage_wrap(DBusMessage_wrap&&) = default;

    operator bool() const noexcept { return m_msg != nullptr; }

    template <class T>
    DBusMessage_wrap& argument(T arg);

    DBusMessage_wrap send_with_reply_and_block(DBusConnection* conn,
                                               int timeout);

    DBusMessageIter_wrap iter() { return DBusMessageIter_wrap(m_msg, m_DBus); }

    static DBusMessage_wrap new_method_call(const std::string& bus_name,
                                            const std::string& path,
                                            const std::string& iface,
                                            const std::string& method,
                                            libdbus_loader* loader);

   private:
    void free_if_owning();
    bool m_owning;
    DBusMessage* m_msg;
    libdbus_loader* m_DBus;
    std::vector<std::string> m_args;
};

template <class T>
DBusMessage_wrap& DBusMessage_wrap::argument(T arg) {
    if (not m_msg) return *this;
    if (not m_DBus->message_append_args(m_msg, detail::dbus_type_identifier<T>,
                                        &arg, DBUS_TYPE_INVALID)) {
        free_if_owning();
    }
    return *this;
}

template <>
DBusMessage_wrap& DBusMessage_wrap::argument<const std::string&>(
    const std::string& str) {
    return argument<const char*>(str.c_str());
}

DBusMessage_wrap DBusMessage_wrap::send_with_reply_and_block(
    DBusConnection* conn, int timeout) {
    if (not m_msg) {
        return DBusMessage_wrap(nullptr, m_DBus);
    }
    DBusError err;
    m_DBus->error_init(&err);
    auto reply = m_DBus->connection_send_with_reply_and_block(conn, m_msg,
                                                              timeout, &err);
    if (reply == nullptr) {
        std::cerr << "MangoHud[" << __func__ << "]: " << err.message << "\n";
        free_if_owning();
        m_DBus->error_free(&err);
    }
    return DBusMessage_wrap(reply, m_DBus, true);
}

DBusMessage_wrap DBusMessage_wrap::new_method_call(const std::string& bus_name,
                                                   const std::string& path,
                                                   const std::string& iface,
                                                   const std::string& method,
                                                   libdbus_loader* loader) {
    auto msg = loader->message_new_method_call(
        (bus_name.empty()) ? nullptr : bus_name.c_str(), path.c_str(),
        (iface.empty()) ? nullptr : iface.c_str(), method.c_str());
    return DBusMessage_wrap(msg, loader, true);
}

void DBusMessage_wrap::free_if_owning() {
    if (m_msg and m_owning) {
        m_DBus->message_unref(m_msg);
    }
    m_msg = nullptr;
}
}  // namespace DBus_helpers

#endif  // MANGOHUD_DBUS_HELPERS