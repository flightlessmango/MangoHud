#include <array>
#include <cstdio>
#include <iostream>
#include <sstream>

#include "dbus_helpers.hpp"
#include "dbus_info.h"
#include "string_utils.h"

using ms = std::chrono::milliseconds;
using namespace DBus_helpers;
#define DBUS_TIMEOUT 2000  // ms

struct mutexed_metadata main_metadata;

namespace dbusmgr {
dbus_manager dbus_mgr;
}

template <class T>
static void assign_metadata_value(metadata& meta, const std::string& key,
                                  const T& value) {
    if (key == "PlaybackStatus") {
        meta.playing = (value == "Playing");
        meta.got_playback_data = true;
    } else if (key == "xesam:title") {
        meta.title = value;
        meta.got_song_data = true;
        meta.valid = true;
    } else if (key == "xesam:artist") {
        meta.artists = value;
        meta.got_song_data = true;
        meta.valid = true;
    } else if (key == "xesam:album") {
        meta.album = value;
        meta.got_song_data = true;
        meta.valid = true;
    } else if (key == "mpris:artUrl") {
        meta.artUrl = value;
        meta.got_song_data = true;
    } else if (key == "xesam:url") {
        // HACK if there's no metadata then use this to clear old ones
        meta.got_song_data = true;
    }
}

std::string format_signal(const dbusmgr::DBusSignal& s) {
    std::stringstream ss;
    ss << "type='signal',interface='" << s.intf << "'";
    ss << ",member='" << s.signal << "'";
    return ss.str();
}

void parse_song_data(DBusMessageIter_wrap iter, metadata& meta){
    iter.string_map_for_each([&meta](const std::string& key,
                                       DBusMessageIter_wrap it) {
        std::string val;
        if (it.is_primitive()) {
            val = it.get_stringified();
        } else if (it.is_array()) {
            it.array_for_each_stringify([&](const std::string& str) {
                if (val.empty()) {
                    val = str;
                } else {
                    val += ", " + str;
                }
            });
        }
        assign_metadata_value(meta, key, val);
    });
}

static void parse_mpris_properties(libdbus_loader& dbus, DBusMessage* msg,
                                   std::string& source, metadata& meta) {
    /**
     *  Expected response Format:
     *      string,
     *      map{
     *          "Metadata" -> multimap,
     *          "PlaybackStatus" -> string
     *      }
     */

    auto iter = DBusMessageIter_wrap(msg, &dbus);
    source = iter.get_primitive<std::string>();
    if (source != "org.mpris.MediaPlayer2.Player") return;

    iter.next();
    if (not iter.is_array()) return;

    iter.string_map_for_each([&meta](std::string& key, DBusMessageIter_wrap it) {
        if (key == "Metadata") {
            parse_song_data(it, meta);
        } else if (key == "PlaybackStatus") {
            auto val = it.get_stringified();
            assign_metadata_value(meta, key, val);
        }
    });
    meta.valid = (meta.artists.size() || !meta.title.empty());
}

bool dbus_get_name_owner(dbusmgr::dbus_manager& dbus_mgr,
                         std::string& name_owner, const char* name) {
    auto reply =
        DBusMessage_wrap::new_method_call(
            "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", "GetNameOwner", &dbus_mgr.dbus())
            .argument(name)
            .send_with_reply_and_block(dbus_mgr.get_conn(), DBUS_TIMEOUT);
    if (not reply) return false;

    auto iter = reply.iter();
    if (not iter.is_string()) return false;
    name_owner = iter.get_primitive<std::string>();
    return true;
}

bool dbus_get_player_property(dbusmgr::dbus_manager& dbus_mgr, metadata& meta,
                              const char* dest, const char* prop) {
    auto reply =
        DBusMessage_wrap::new_method_call(dest, "/org/mpris/MediaPlayer2",
                                          "org.freedesktop.DBus.Properties",
                                          "Get", &dbus_mgr.dbus())
            .argument("org.mpris.MediaPlayer2.Player")
            .argument(prop)
            .send_with_reply_and_block(dbus_mgr.get_conn(), DBUS_TIMEOUT);

    if (not reply) return false;

    auto iter = reply.iter();

    if (iter.is_array()) {
        parse_song_data(iter, meta);
    } else if (iter.is_primitive()) {
        assign_metadata_value(meta, prop, iter.get_stringified());
    } else {
        return false;
    }
    return true;
}

namespace dbusmgr {
bool dbus_manager::get_media_player_metadata(metadata& meta, std::string name) {
    if (name == "") name = m_active_player;
    if (name == "") return false;
    meta.clear();
    dbus_get_player_property(*this, meta, name.c_str(), "Metadata");
    dbus_get_player_property(*this, meta, name.c_str(), "PlaybackStatus");
    meta.valid = (meta.artists.size() || !meta.title.empty());
    return true;
}

bool dbus_manager::init(const std::string& requested_player) {
    if (m_inited) return true;

    if (not requested_player.empty()) {
        m_requested_player = "org.mpris.MediaPlayer2." + requested_player;
    }

    if (!m_dbus_ldr.IsLoaded() && !m_dbus_ldr.Load("libdbus-1.so.3")) {
        std::cerr << "MANGOHUD: Could not load libdbus-1.so.3\n";
        return false;
    }

    m_dbus_ldr.error_init(&m_error);

    m_dbus_ldr.threads_init_default();

    if (nullptr ==
        (m_dbus_conn = m_dbus_ldr.bus_get(DBUS_BUS_SESSION, &m_error))) {
        std::cerr << "MANGOHUD: " << m_error.message << std::endl;
        m_dbus_ldr.error_free(&m_error);
        return false;
    }

    std::cout << "MANGOHUD: Connected to D-Bus as \""
              << m_dbus_ldr.bus_get_unique_name(m_dbus_conn) << "\"."
              << std::endl;

    dbus_list_name_to_owner();
    connect_to_signals();
    select_active_player();

    m_inited = true;
    return true;
}

bool dbus_manager::select_active_player() {
    auto old_active_player = m_active_player;
    m_active_player = "";
    metadata meta {};
    if (not m_requested_player.empty()) {
        // If the requested player is available, use it
        if (m_name_owners.count(m_requested_player) > 0) {
            m_active_player = m_requested_player;
#ifndef NDEBUG
            std::cerr << "Selecting requested player: " << m_requested_player
                      << "\n";
#endif
            get_media_player_metadata(meta, m_active_player);
        }
    } else {
        // If no player is requested, use any player that is currently playing
        if (m_active_player.empty()) {
            auto it = std::find_if(m_name_owners.begin(), m_name_owners.end(), [this, &meta](auto& entry){
                auto& name = entry.first;
                get_media_player_metadata(meta, name);
                if(meta.playing) {
                    return true;
                }
                else {
                    meta = {};
                    return false;
                }
            });

            if(it != m_name_owners.end()){
                m_active_player = it->first;
#ifndef NDEBUG
                std::cerr << "Selecting fallback player: " << m_active_player << "\n";
#endif
            }
        }
    }

    if (not m_active_player.empty()) {
        onNewPlayer(meta);
        return true;
    } else {
#ifndef NDEBUG
        std::cerr << "No active players\n";
#endif
        if (not old_active_player.empty()) {
            onNoPlayer();
        }
        return false;
    }
}

void dbus_manager::deinit() {
    if (!m_inited) return;

    // unreference system bus connection instead of closing it
    if (m_dbus_conn) {
        disconnect_from_signals();
        m_dbus_ldr.connection_unref(m_dbus_conn);
        m_dbus_conn = nullptr;
    }
    m_dbus_ldr.error_free(&m_error);
    m_inited = false;
}

dbus_manager::~dbus_manager() { deinit(); }

DBusHandlerResult dbus_manager::filter_signals(DBusConnection* conn,
                                               DBusMessage* msg,
                                               void* userData) {
    auto& manager = *reinterpret_cast<dbus_manager*>(userData);

    for (auto& sig : manager.m_signals) {
        if (manager.m_dbus_ldr.message_is_signal(msg, sig.intf, sig.signal)) {
            auto sender = manager.m_dbus_ldr.message_get_sender(msg);
            if ((manager.*(sig.handler))(msg, sender))
                return DBUS_HANDLER_RESULT_HANDLED;
            else
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

bool dbus_manager::handle_properties_changed(DBusMessage* msg,
                                             const char* sender) {
    std::string source;

    metadata meta;
    parse_mpris_properties(m_dbus_ldr, msg, source, meta);
#ifndef NDEBUG
    std::cerr << "PropertiesChanged Signal received:\n";
    std::cerr << "\tSource: " << source << "\n";
    std::cerr << "active_player:         " << m_active_player << "\n";
    std::cerr << "active_player's owner: " << m_name_owners[m_active_player]
              << "\n";
    std::cerr << "sender:                " << sender << "\n";
#endif
    if (source != "org.mpris.MediaPlayer2.Player") return false;

    if (m_active_player == "" or
        (m_requested_player.empty() and not main_metadata.meta.playing)) {
        select_active_player();
    }
    else if (m_name_owners[m_active_player] == sender) {
        onPlayerUpdate(meta);
    }
    return true;
}

bool dbus_manager::handle_name_owner_changed(DBusMessage* _msg,
                                             const char* sender) {
    std::vector<std::string> str;

    for (auto iter = DBusMessageIter_wrap(_msg, &m_dbus_ldr); iter;
         iter.next()) {
        str.push_back(iter.get_primitive<std::string>());
    }

    // register new name
    if (str.size() == 3 && starts_with(str[0], "org.mpris.MediaPlayer2.") &&
        !str[2].empty()) {
        m_name_owners[str[0]] = str[2];
        if (str[0] == m_requested_player) {
            select_active_player();
        }
    }

    // did a player quit?
    if (str[2].empty()) {
        if (str.size() == 3 && str[0] == m_active_player) {
            m_name_owners.erase(str[0]);
            select_active_player();
        }
    }
    return true;
}

void dbus_manager::connect_to_signals() {
    for (auto kv : m_signals) {
        auto signal = format_signal(kv);
        m_dbus_ldr.bus_add_match(m_dbus_conn, signal.c_str(), &m_error);
        if (m_dbus_ldr.error_is_set(&m_error)) {
            ::perror(m_error.name);
            ::perror(m_error.message);
            m_dbus_ldr.error_free(&m_error);
            // return;
        }
    }
    m_dbus_ldr.connection_add_filter(m_dbus_conn, filter_signals,
                                     reinterpret_cast<void*>(this), nullptr);

    start_thread();
}

void dbus_manager::disconnect_from_signals() {
    m_dbus_ldr.connection_remove_filter(m_dbus_conn, filter_signals,
                                        reinterpret_cast<void*>(this));
    for (auto kv : m_signals) {
        auto signal = format_signal(kv);
        m_dbus_ldr.bus_remove_match(m_dbus_conn, signal.c_str(), &m_error);
        if (m_dbus_ldr.error_is_set(&m_error)) {
            ::perror(m_error.name);
            ::perror(m_error.message);
            m_dbus_ldr.error_free(&m_error);
        }
    }

    stop_thread();
}

bool dbus_manager::dbus_list_name_to_owner() {
    auto reply =
        DBusMessage_wrap::new_method_call(
            "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", "ListNames", &dbus_mgr.dbus())
            .send_with_reply_and_block(dbus_mgr.get_conn(), DBUS_TIMEOUT);
    if (not reply) return false;

    auto iter = reply.iter();

    if (not iter.is_array()) {
        return false;
    }
    iter.array_for_each_value<std::string>([&](std::string name) {
        if (!starts_with(name.c_str(), "org.mpris.MediaPlayer2.")) return;
        std::string owner;
        if (dbus_get_name_owner(dbus_mgr, owner, name.c_str())) {
            m_name_owners[name] = owner;
        }
    });
    return true;
}

void dbus_manager::stop_thread() {
    m_quit = true;
    if (m_thread.joinable()) m_thread.join();
}

void dbus_manager::start_thread() {
    stop_thread();
    m_quit = false;
    m_thread = std::thread(&dbus_manager::dbus_thread, this);
}

void dbus_manager::dbus_thread() {
    using namespace std::chrono_literals;
    while (!m_quit && m_dbus_ldr.connection_read_write_dispatch(m_dbus_conn, 0))
        std::this_thread::sleep_for(10ms);
}

void dbus_manager::onNoPlayer() {
    std::lock_guard<std::mutex> lck(main_metadata.mtx);
    main_metadata.meta = {};
    main_metadata.ticker = {};
}

void dbus_manager::onNewPlayer(metadata& meta) {
    std::lock_guard<std::mutex> lck(main_metadata.mtx);
    main_metadata.meta = meta;
    main_metadata.ticker = {};
}

void dbus_manager::onPlayerUpdate(metadata& meta) {
    std::lock_guard<std::mutex> lck(main_metadata.mtx);
    if (meta.got_song_data) {
        // If the song has changed, reset the ticker
        if (main_metadata.meta.artists != meta.artists ||
            main_metadata.meta.album != meta.album ||
            main_metadata.meta.title != meta.title) {
            main_metadata.ticker = {};
        }

        main_metadata.meta = meta;
        main_metadata.meta.playing = true;
    }
    if (meta.got_playback_data) {
        main_metadata.meta.playing = meta.playing;
    }
}

}  // namespace dbusmgr
