#pragma once
#include <dbus/dbus.h>
#include <stdexcept>
#include <thread>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <mutex>

struct metadata {
    std::vector<std::string> artists;
    std::string title;
    std::string album;
    std::string something;
    std::string artUrl;
    bool playing = false;

    bool valid = false;
    std::mutex mutex;
};

enum SignalType
{
    ST_NAMEOWNERCHANGED,
    ST_PROPERTIESCHANGED,
};

struct DBusSignal
{
    const char * intf;
    const char * signal;
    SignalType type;
};

extern struct metadata spotify;

namespace dbusmgr {
    using callback_func = std::function<void(/*metadata*/)>;

    enum CBENUM {
        CB_CONNECTED,
        CB_DISCONNECTED,
        CB_NEW_METADATA,
    };

    class dbus_error : public std::runtime_error
    {
    public:
        dbus_error(DBusError *src) : std::runtime_error(src->message)
        {
            dbus_error_init(&error);
            dbus_move_error (src, &error);
        }
        virtual ~dbus_error() { dbus_error_free (&error); }
    private:
        DBusError error;
    };

    class dbus_manager
    {
    public:
        dbus_manager()
        {
            ::dbus_error_init(&m_error);
        }

        ~dbus_manager();

        void init();
        void add_callback(CBENUM type, callback_func func);
        void connect_to_signals();
        void disconnect_from_signals();
        DBusConnection* get_conn() const {
            return m_dbus_conn;
        }

    protected:
        void stop_thread();
        void start_thread();
        static void dbus_thread(dbus_manager *pmgr);

        DBusError m_error;
        DBusConnection * m_dbus_conn = nullptr;
        DBusMessage * m_dbus_msg = nullptr;
        DBusMessage * m_dbus_reply = nullptr;
        bool m_quit = false;
        bool m_inited = false;
        std::thread m_thread;
        std::map<CBENUM, callback_func> m_callbacks;

        const std::array<DBusSignal, 2> m_signals {{
            { "org.freedesktop.DBus", "NameOwnerChanged", ST_NAMEOWNERCHANGED },
            { "org.freedesktop.DBus.Properties", "PropertiesChanged", ST_PROPERTIESCHANGED },
        }};

    };

    extern dbus_manager dbus_mgr;
}

void get_spotify_metadata(dbusmgr::dbus_manager& dbus, metadata& meta);
