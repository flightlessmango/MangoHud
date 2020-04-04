#pragma once
#include <stdexcept>
#include <thread>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include "loaders/loader_dbus.h"

struct metadata {
    //std::vector<std::string> artists;
    std::string artists; // pre-concatenate
    std::string title;
    std::string album;
    std::string something;
    std::string artUrl;
    bool playing = false;
    struct {
        float pos;
        float longest;
        int dir = -1;
        bool needs_recalc;

        float tw0;
        float tw1;
        float tw2;
    } ticker;

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
        dbus_error(libdbus_loader& dbus_, DBusError *src) : std::runtime_error(src->message)
        {
            dbus = &dbus_;
            dbus->error_init(&error);
            dbus->move_error (src, &error);
        }
        virtual ~dbus_error() { dbus->error_free (&error); }
    private:
        DBusError error;
        libdbus_loader *dbus;
    };

    class dbus_manager
    {
    public:
        dbus_manager()
        {
        }

        ~dbus_manager();

        void init();
        void add_callback(CBENUM type, callback_func func);
        void connect_to_signals();
        void disconnect_from_signals();
        DBusConnection* get_conn() const {
            return m_dbus_conn;
        }

        libdbus_loader& dbus() {
            return m_dbus_ldr;
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
        libdbus_loader m_dbus_ldr;

        const std::array<DBusSignal, 2> m_signals {{
            { "org.freedesktop.DBus", "NameOwnerChanged", ST_NAMEOWNERCHANGED },
            { "org.freedesktop.DBus.Properties", "PropertiesChanged", ST_PROPERTIESCHANGED },
        }};

    };

    extern dbus_manager dbus_mgr;
}

void get_spotify_metadata(dbusmgr::dbus_manager& dbus, metadata& meta);
