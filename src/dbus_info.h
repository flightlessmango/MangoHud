#pragma once
#ifndef MANGOHUD_DBUS_INFO_H
#define MANGOHUD_DBUS_INFO_H

#include <array>
#include <stdexcept>
#include <thread>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <mutex>
#include "loaders/loader_dbus.h"

typedef std::unordered_map<std::string, std::string> string_map;

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

    void clear()
    {
        artists.clear();
        title.clear();
        album.clear();
        artUrl.clear();
        ticker = {};
        ticker.dir = -1;
        valid = false;
    }
};

enum SignalType
{
    ST_NAMEOWNERCHANGED,
    ST_PROPERTIESCHANGED,
};


extern struct metadata main_metadata;

namespace dbusmgr {

    class dbus_manager;
    using signal_handler_func = bool (dbus_manager::*)(DBusMessage*, const char*);

    struct DBusSignal
    {
        const char * intf;
        const char * signal;
        signal_handler_func handler;
    };

    using callback_func = std::function<void(/*metadata*/)>;

    enum CBENUM {
        CB_CONNECTED,
        CB_DISCONNECTED,
        CB_NEW_METADATA,
    };

    class dbus_manager
    {
    public:
        dbus_manager()
        {
        }

        ~dbus_manager();

        bool init(const std::string& requested_player);
        void deinit();
        bool get_media_player_metadata(metadata& meta, std::string name = "");
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
        void dbus_thread();

        bool dbus_list_name_to_owner();
        bool select_active_player();

        static DBusHandlerResult filter_signals(DBusConnection*, DBusMessage*, void*);

        bool handle_properties_changed(DBusMessage*, const char*);
        bool handle_name_owner_changed(DBusMessage*, const char*);

        DBusError m_error;
        DBusConnection * m_dbus_conn = nullptr;
        DBusMessage * m_dbus_msg = nullptr;
        DBusMessage * m_dbus_reply = nullptr;
        bool m_quit = false;
        bool m_inited = false;
        std::thread m_thread;
        std::map<CBENUM, callback_func> m_callbacks;
        libdbus_loader m_dbus_ldr;
        std::unordered_map<std::string, std::string> m_name_owners;
        std::string m_requested_player;
        std::string m_active_player; 



        const std::array<DBusSignal, 2> m_signals {{
            { "org.freedesktop.DBus", "NameOwnerChanged", &dbus_manager::handle_name_owner_changed },
            { "org.freedesktop.DBus.Properties", "PropertiesChanged", &dbus_manager::handle_properties_changed },
        }};

    };

    extern dbus_manager dbus_mgr;
}

<<<<<<< HEAD
bool get_media_player_metadata(dbusmgr::dbus_manager& dbus, const std::string& name, metadata& meta);

#endif //MANGOHUD_DBUS_INFO_H
=======
//bool get_media_player_metadata(dbusmgr::dbus_manager& dbus, const std::string& name, metadata& meta);
>>>>>>> 9c064df... Change the media player functionality to allow changing active media
