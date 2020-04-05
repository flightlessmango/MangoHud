#include <cstdio>
#include <iostream>
#include <sstream>
#include <array>
#include "dbus_info.h"

using ms = std::chrono::milliseconds;

struct metadata spotify;
typedef std::vector<std::pair<std::string, std::string>> string_pair_vec;

std::string format_signal(const DBusSignal& s)
{
    std::stringstream ss;
    ss << "type='signal',interface='" << s.intf << "'";
    ss << ",member='" << s.signal << "'";
    return ss.str();
}

static bool check_msg_arg(libdbus_loader& dbus, DBusMessageIter *iter, int type)
{
    int curr_type = DBUS_TYPE_INVALID;
    if ((curr_type = dbus.message_iter_get_arg_type (iter)) != type) {
#ifndef NDEBUG
        std::cerr << "Argument is not of type '" << (char)type << "' != '" << (char) curr_type << "'" << std::endl;
#endif
        return false;
    }
    return true;
}

bool get_string_array(libdbus_loader& dbus, DBusMessageIter *iter_, std::vector<std::string>& entries)
{
    DBusMessageIter iter = *iter_;
    DBusMessageIter subiter;
    int current_type = DBUS_TYPE_INVALID;

    current_type = dbus.message_iter_get_arg_type (&iter);
    if (current_type == DBUS_TYPE_VARIANT) {
        dbus.message_iter_recurse (&iter, &iter);
        current_type = dbus.message_iter_get_arg_type (&iter);
    }

    if (current_type != DBUS_TYPE_ARRAY) {
#ifndef NDEBUG
        std::cerr << "Not an array: '" << (char)current_type << "'" << std::endl;
#endif
        return false;
    }

    char *val = nullptr;

    dbus.message_iter_recurse (&iter, &subiter);
    entries.clear();
    while ((current_type = dbus.message_iter_get_arg_type (&subiter)) != DBUS_TYPE_INVALID) {
        if (current_type == DBUS_TYPE_STRING)
        {
            dbus.message_iter_get_basic (&subiter, &val);
            entries.push_back(val);
        }
        dbus.message_iter_next (&subiter);
    }
    return true;
}

static bool get_variant_string(libdbus_loader& dbus, DBusMessageIter *iter_, std::string &val, bool key_or_value = false)
{
    DBusMessageIter iter = *iter_;
    char *str = nullptr;
    int type = dbus.message_iter_get_arg_type (&iter);
    if (type != DBUS_TYPE_VARIANT && type != DBUS_TYPE_DICT_ENTRY)
        return false;

    dbus.message_iter_recurse (&iter, &iter);

    if (key_or_value) {
        dbus.message_iter_next (&iter);
        if (!check_msg_arg (dbus, &iter, DBUS_TYPE_VARIANT))
            return false;
        dbus.message_iter_recurse (&iter, &iter);
    }

    if (!check_msg_arg (dbus, &iter, DBUS_TYPE_STRING))
        return false;

    dbus.message_iter_get_basic(&iter, &str);
    val = str;

    return true;
}

static bool get_variant_string(libdbus_loader& dbus, DBusMessage *msg, std::string &val, bool key_or_value = false)
{
    DBusMessageIter iter;
    dbus.message_iter_init (msg, &iter);
    return get_variant_string(dbus, &iter, val, key_or_value);
}

static void parse_mpris_metadata(libdbus_loader& dbus, DBusMessageIter *iter_, string_pair_vec& entries)
{
    DBusMessageIter subiter, iter = *iter_;
    std::string key, val;
    std::vector<std::string> list;

    while (dbus.message_iter_get_arg_type(&iter) != DBUS_TYPE_INVALID)
    {
        dbus.message_iter_next (&iter);
        //std::cerr << "\ttype: " << (char)dbus.message_iter_get_arg_type(&iter) << std::endl;
        if (!get_variant_string(dbus, &iter, key))
            return;

        dbus.message_iter_recurse (&iter, &subiter);
        dbus.message_iter_next (&subiter);

        //std::cerr << "\tkey: " << key << std::endl;
        if (get_variant_string(dbus, &subiter, val)) {
            //std::cerr << "\t\t" << val << std::endl;
            entries.push_back({key, val});
        }
        else if (get_string_array(dbus, &subiter, list)) {
            for (auto& s : list) {
                //std::cerr << "\t\t" << s << std::endl;
                entries.push_back({key, s});
            }
        }
    }
}

static void parse_mpris_properties(libdbus_loader& dbus, DBusMessage *msg, std::string& source, string_pair_vec& entries)
{
    const char *val_char = nullptr;
    DBusMessageIter iter;
    std::string key, val;

    std::vector<DBusMessageIter> stack;
    stack.push_back({});

    dbus.message_iter_init (msg, &stack.back());

    // Should be 'org.mpris.MediaPlayer2.Player'
    if (!check_msg_arg(dbus, &stack.back(), DBUS_TYPE_STRING))
        return;

    dbus.message_iter_get_basic(&stack.back(), &val_char);
    source = val_char;

    if (source != "org.mpris.MediaPlayer2.Player")
        return;

    dbus.message_iter_next (&stack.back());
    //std::cerr << "type: " << (char)dbus.message_iter_get_arg_type(&stack.back()) << std::endl;
    if (!check_msg_arg(dbus, &stack.back(), DBUS_TYPE_ARRAY))
        return;

    dbus.message_iter_recurse (&stack.back(), &iter);
    stack.push_back(iter);

    while (dbus.message_iter_get_arg_type(&stack.back()) != DBUS_TYPE_INVALID)
    {
        if (!get_variant_string(dbus, &stack.back(), key)) {
            dbus.message_iter_next (&stack.back());
            continue;
        }

        if (key == "Metadata") {
#ifndef NDEBUG
            std::cerr << __func__ << ": Found Metadata!" << std::endl;
#endif

            // dive into Metadata
            dbus.message_iter_recurse (&stack.back(), &iter);

            // get the array of entries
            dbus.message_iter_next (&iter);
            if (!check_msg_arg(dbus, &iter, DBUS_TYPE_VARIANT))
                continue;
            dbus.message_iter_recurse (&iter, &iter);

            if (!check_msg_arg(dbus, &iter, DBUS_TYPE_ARRAY))
                continue;
            dbus.message_iter_recurse (&iter, &iter);

            parse_mpris_metadata(dbus, &iter, entries);
        }
        else if (key == "PlaybackStatus") {
            dbus.message_iter_recurse (&stack.back(), &iter);
            dbus.message_iter_next (&iter);

            if (get_variant_string(dbus, &iter, val))
                entries.push_back({key, val});
        }

        dbus.message_iter_next (&stack.back());
    }
}

static void parse_property_changed(libdbus_loader& dbus, DBusMessage *msg, std::string& source, string_pair_vec& entries)
{
    char *name = nullptr;
    int i;
    uint64_t u64;
    double d;

    std::vector<DBusMessageIter> stack;
    stack.push_back({});

#ifndef NDEBUG
    std::vector<char> padding;
    padding.push_back('\0');
#endif

    dbus.message_iter_init (msg, &stack.back());
    int type, prev_type = 0;

    type = dbus.message_iter_get_arg_type (&stack.back());
    if (type != DBUS_TYPE_STRING) {
#ifndef NDEBUG
        std::cerr << __func__ << "First element is not a string" << std::endl;
#endif
        return;
    }

    dbus.message_iter_get_basic(&stack.back(), &name);
    source = name;
#ifndef NDEBUG
    std::cout << name << std::endl;
#endif

    std::pair<std::string, std::string> kv;

    dbus.message_iter_next (&stack.back());
    // the loop should be able parse the whole message if used for generic use-cases
    while ((type = dbus.message_iter_get_arg_type (&stack.back())) != DBUS_TYPE_INVALID) {
#ifndef NDEBUG
        padding.back() = ' ';
        padding.resize(stack.size() + 1, ' ');
        padding.back() = '\0';
        std::cout << padding.data() << "Type: " << (char)type;
#endif

        if (type == DBUS_TYPE_STRING) {
            dbus.message_iter_get_basic(&stack.back(), &name);
#ifndef NDEBUG
            std::cout << "=" << name << std::endl;
#endif
            if (prev_type == DBUS_TYPE_DICT_ENTRY) // is key ?
                kv.first = name;
            if (prev_type == DBUS_TYPE_VARIANT || prev_type == DBUS_TYPE_ARRAY) { // is value ?
                kv.second = name;
                entries.push_back(kv);
            }
        }
        else if (type == DBUS_TYPE_INT32) {
            dbus.message_iter_get_basic(&stack.back(), &i);
#ifndef NDEBUG
            std::cout << "=" << i << std::endl;
#endif
        }
        else if (type == DBUS_TYPE_UINT64) {
            dbus.message_iter_get_basic(&stack.back(), &u64);
#ifndef NDEBUG
            std::cout << "=" << u64 << std::endl;
#endif
        }
        else if (type == DBUS_TYPE_DOUBLE) {
            dbus.message_iter_get_basic(&stack.back(), &d);
#ifndef NDEBUG
            std::cout << "=" << d << std::endl;
#endif
        }
        else if (type == DBUS_TYPE_ARRAY || type == DBUS_TYPE_DICT_ENTRY || type == DBUS_TYPE_VARIANT) {
#ifndef NDEBUG
            std::cout << std::endl;
#endif
            prev_type = type;
            DBusMessageIter iter;
            dbus.message_iter_recurse (&stack.back(), &iter);
            if (dbus.message_iter_get_arg_type (&stack.back()) != DBUS_TYPE_INVALID)
                stack.push_back(iter);
            continue;
        } else {
#ifndef NDEBUG
            std::cout << std::endl;
#endif
        }

        while(FALSE == dbus.message_iter_next (&stack.back()) && stack.size() > 1) {
            stack.pop_back();
            prev_type = 0;
        }
    }
}

bool get_dict_string_array(libdbus_loader& dbus, DBusMessage *msg, string_pair_vec& entries)
{
    DBusMessageIter iter, outer_iter;
    dbus.message_iter_init (msg, &outer_iter);
    int current_type = DBUS_TYPE_INVALID;

    current_type = dbus.message_iter_get_arg_type (&outer_iter);

    if (current_type == DBUS_TYPE_VARIANT) {
        dbus.message_iter_recurse (&outer_iter, &outer_iter);
        current_type = dbus.message_iter_get_arg_type (&outer_iter);
    }

    if (current_type != DBUS_TYPE_ARRAY) {
#ifndef NDEBUG
        std::cerr << "Not an array " << (char)current_type << std::endl;
#endif
        return false;
    }

    char *val_key = nullptr, *val_value = nullptr;

    dbus.message_iter_recurse (&outer_iter, &outer_iter);
    while ((current_type = dbus.message_iter_get_arg_type (&outer_iter)) != DBUS_TYPE_INVALID) {
        // printf("type: %d\n", current_type);

        if (current_type == DBUS_TYPE_DICT_ENTRY)
        {
            dbus.message_iter_recurse (&outer_iter, &iter);

            // dict entry key
            //printf("\tentry: {%c, ", dbus.message_iter_get_arg_type (&iter));
            dbus.message_iter_get_basic (&iter, &val_key);
            std::string key = val_key;

            // dict entry value
            dbus.message_iter_next (&iter);

            if (dbus.message_iter_get_arg_type (&iter) == DBUS_TYPE_VARIANT)
                dbus.message_iter_recurse (&iter, &iter);

            if (dbus.message_iter_get_arg_type (&iter) == DBUS_TYPE_ARRAY) {
                dbus.message_iter_recurse (&iter, &iter);
                if (dbus.message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING) {
                    //printf("%c}\n", dbus.message_iter_get_arg_type (&iter));
                    dbus.message_iter_get_basic (&iter, &val_value);
                    entries.push_back({val_key, val_value});
                }
            }
            else if (dbus.message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING) {
                //printf("%c}\n", dbus.message_iter_get_arg_type (&iter));
                dbus.message_iter_get_basic (&iter, &val_value);
                entries.push_back({val_key, val_value});
            }
        }
        dbus.message_iter_next (&outer_iter);
    }
    return true;
}

static void assign_metadata(metadata& meta, string_pair_vec& entries)
{
    meta.title.clear();
    meta.artists.clear();
    meta.album.clear();

    std::lock_guard<std::mutex> lk(meta.mutex);
    std::vector<std::string> artists;
    meta.valid = false;
    for (auto& kv : entries) {
#ifndef NDEBUG
        std::cerr << kv.first << " = " << kv.second << std::endl;
#endif
        if (kv.first == "xesam:artist") {
            artists.push_back(kv.second);
        }
        else if (kv.first == "xesam:title")
            meta.title = kv.second;
        else if (kv.first == "xesam:album")
            meta.album = kv.second;
        else if (kv.first == "mpris:artUrl")
            meta.artUrl = kv.second;
        else if (kv.first == "PlaybackStatus")
            meta.playing = (kv.second == "Playing");
    }

    // XXX Spotify only sends one artist anyway
    for (auto p = artists.begin(); p != artists.end(); p++) {
        meta.artists += *p;
        if (p != artists.end() - 1)
            meta.artists += ", ";
    }

    if (meta.artists.size() || !meta.title.empty())
        meta.valid = meta.playing;

    meta.ticker.needs_recalc = true;
    meta.ticker.pos = 0;
    meta.ticker.longest = 0;
    meta.ticker.dir = -1;
}

void dbus_get_spotify_property(dbusmgr::dbus_manager& dbus_mgr, string_pair_vec& entries, const char * prop)
{
    auto& dbus = dbus_mgr.dbus();
    DBusError error;
    dbus.error_init(&error);

    DBusMessage * dbus_reply = nullptr;
    DBusMessage * dbus_msg = nullptr;

    // dbus-send --print-reply --session --dest=org.mpris.MediaPlayer2.spotify /org/mpris/MediaPlayer2 org.freedesktop.DBus.Properties.Get string:'org.mpris.MediaPlayer2.Player' string:'Metadata'
    if (nullptr == (dbus_msg = dbus.message_new_method_call("org.mpris.MediaPlayer2.spotify", "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "Get"))) {
       throw std::runtime_error("unable to allocate memory for dbus message");
    }

    const char *v_STRINGS[] = {
        "org.mpris.MediaPlayer2.Player",
    };

    std::cerr << __func__ << ": " << prop << std::endl;
    if (!dbus.message_append_args (dbus_msg, DBUS_TYPE_STRING, &v_STRINGS[0], DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID)) {
        dbus.message_unref(dbus_msg);
        throw std::runtime_error(error.message);
    }

    if (nullptr == (dbus_reply = dbus.connection_send_with_reply_and_block(dbus_mgr.get_conn(), dbus_msg, DBUS_TIMEOUT_USE_DEFAULT, &error))) {
        dbus.message_unref(dbus_msg);
        throw dbusmgr::dbus_error(dbus, &error);
    }

    std::string entry;
    if (get_dict_string_array(dbus, dbus_reply, entries)) {
        // nothing
    } else if (get_variant_string(dbus, dbus_reply, entry)) {
        entries.push_back({prop, entry});
    }

    dbus.message_unref(dbus_msg);
    dbus.message_unref(dbus_reply);
    dbus.error_free(&error);
}

void get_spotify_metadata(dbusmgr::dbus_manager& dbus, metadata& meta)
{
    meta.artists.clear();
    string_pair_vec entries;
    dbus_get_spotify_property(dbus, entries, "Metadata");
    dbus_get_spotify_property(dbus, entries, "PlaybackStatus");
    assign_metadata(meta, entries);
}

namespace dbusmgr {
void dbus_manager::init()
{
    if (m_inited)
        return;

    if (!m_dbus_ldr.Load("libdbus-1.so.3"))
        throw std::runtime_error("Could not load libdbus-1.so.3");

    m_dbus_ldr.error_init(&m_error);

    m_dbus_ldr.threads_init_default();

    if ( nullptr == (m_dbus_conn = m_dbus_ldr.bus_get(DBUS_BUS_SESSION, &m_error)) ) {
        throw dbus_error(m_dbus_ldr, &m_error);
    }
    std::cout << "Connected to D-Bus as \"" << m_dbus_ldr.bus_get_unique_name(m_dbus_conn) << "\"." << std::endl;

    connect_to_signals();
    m_inited = true;
}

dbus_manager::~dbus_manager()
{
    if (m_inited) {
        // unreference system bus connection instead of closing it
        if (m_dbus_conn) {
            disconnect_from_signals();
            m_dbus_ldr.connection_unref(m_dbus_conn);
            m_dbus_conn = nullptr;
        }
        m_dbus_ldr.error_free(&m_error);
    }
}

void dbus_manager::connect_to_signals()
{
    for (auto kv : m_signals) {
        auto signal = format_signal(kv);
        m_dbus_ldr.bus_add_match(m_dbus_conn, signal.c_str(), &m_error);
        if (m_dbus_ldr.error_is_set(&m_error)) {
            ::perror(m_error.name);
            ::perror(m_error.message);
            m_dbus_ldr.error_free(&m_error);
            //return;
        }
    }

    start_thread();
}

void dbus_manager::disconnect_from_signals()
{
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

void dbus_manager::add_callback(CBENUM type, callback_func func)
{
    m_callbacks[type] = func;
}

void dbus_manager::stop_thread()
{
    m_quit = true;
    if (m_thread.joinable())
        m_thread.join();
}

void dbus_manager::start_thread()
{
    stop_thread();
    m_quit = false;
    m_thread = std::thread(dbus_thread, this);
}

void dbus_manager::dbus_thread(dbus_manager *pmgr)
{
    (void)parse_property_changed;
    DBusMessage *msg = nullptr;
    auto& dbus = pmgr->dbus();

    // loop listening for signals being emmitted
    while (!pmgr->m_quit) {

        // non blocking read of the next available message
        if (!dbus.connection_read_write(pmgr->m_dbus_conn, 0))
            return; // connection closed

        msg = dbus.connection_pop_message(pmgr->m_dbus_conn);

        // loop again if we haven't read a message
        if (nullptr == msg) {
            std::this_thread::sleep_for(ms(10));
            continue;
        }

        for (auto& sig : pmgr->m_signals) {
            if (dbus.message_is_signal(msg, sig.intf, sig.signal))
            {

#ifndef NDEBUG
                std::cerr << __func__ << ": " << sig.intf << "::" << sig.signal << std::endl;
#endif

                switch (sig.type) {
                    case ST_PROPERTIESCHANGED:
                    {
                        std::string source;
                        std::vector<std::pair<std::string, std::string>> entries;

                        //parse_property_changed(msg, source, entries);
                        parse_mpris_properties(dbus, msg, source, entries);
#ifndef NDEBUG
                        std::cerr << "Source: " << source << std::endl;
#endif
                        if (source != "org.mpris.MediaPlayer2.Player")
                            break;
                        assign_metadata(spotify, entries);
                    }
                    break;
                    case ST_NAMEOWNERCHANGED:
                    {
                        DBusMessageIter iter;
                        dbus.message_iter_init (msg, &iter);
                        std::vector<std::string> str;
                        const char *value = nullptr;

                        while (dbus.message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING) {
                            dbus.message_iter_get_basic (&iter, &value);
                            str.push_back(value);
                            dbus.message_iter_next (&iter);
                        }

                        // did spotify quit?
                        if (str.size() == 3
                            && str[0] == "org.mpris.MediaPlayer2.spotify"
                            && str[2].empty()
                        )
                        {
                            spotify.valid = false;
                        }
                    }
                    break;
                    default:
                    break;
                }
            }
        }

        // free the message
        dbus.message_unref(msg);
    }
}

   dbus_manager dbus_mgr;
}