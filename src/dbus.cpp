#include <cstdio>
#include <iostream>
#include <sstream>
#include <array>
#include "dbus_info.h"
#include "string_utils.h"

using ms = std::chrono::milliseconds;
#define DBUS_TIMEOUT 2000 // ms

struct mutexed_metadata main_metadata;

typedef std::vector<std::pair<std::string, std::string>> string_pair_vec;
typedef std::unordered_map<std::string, string_pair_vec> string_pair_vec_map;
typedef std::unordered_map<std::string, std::string> string_map;

namespace dbusmgr { 
dbus_manager dbus_mgr;
}

template<class T> struct dbus_type_identifier{};
template<> struct dbus_type_identifier<uint8_t> { const int value = DBUS_TYPE_BYTE; };
template<> struct dbus_type_identifier<uint16_t> { const int value = DBUS_TYPE_UINT16; };
template<> struct dbus_type_identifier<uint32_t> { const int value = DBUS_TYPE_UINT32; };
template<> struct dbus_type_identifier<uint64_t> { const int value = DBUS_TYPE_UINT64; };
template<> struct dbus_type_identifier<int16_t> { const int value = DBUS_TYPE_INT16; };
template<> struct dbus_type_identifier<int32_t> { const int value = DBUS_TYPE_INT32; };
template<> struct dbus_type_identifier<int64_t> { const int value = DBUS_TYPE_INT64; };
template<> struct dbus_type_identifier<double> { const int value = DBUS_TYPE_DOUBLE; };
template<> struct dbus_type_identifier<const char*> { const int value = DBUS_TYPE_STRING; };

template<class T>
const int dbus_type_identifier_v = dbus_type_identifier<T>().value;

class DBusMessageIter_wrap {
private:
    DBusMessageIter resolve_variants() {
        auto iter = m_Iter;
        auto field_type = m_DBus->message_iter_get_arg_type(&m_Iter);
        while(field_type == DBUS_TYPE_VARIANT){
            m_DBus->message_iter_recurse(&iter, &iter);
            field_type = m_DBus->message_iter_get_arg_type(&iter);
        }
        return iter;
    }

    DBusMessageIter m_Iter;
    DBusMessageIter m_resolved_iter;
    int m_type;
    libdbus_loader* m_DBus;
public:
    DBusMessageIter_wrap(DBusMessage* msg, libdbus_loader* loader)
    {
        m_DBus = loader;
        if(msg){
            m_DBus->message_iter_init(msg, &m_Iter);
            m_resolved_iter = resolve_variants();
            m_type = m_DBus->message_iter_get_arg_type(&m_resolved_iter);
        }
        else {
            m_type = DBUS_TYPE_INVALID;
        }
    }

    DBusMessageIter_wrap(DBusMessageIter iter, libdbus_loader* loader)
        : m_Iter(iter), m_DBus(loader)
    {
        m_resolved_iter = resolve_variants();
        m_type = m_DBus->message_iter_get_arg_type(&m_resolved_iter);
    }

    operator bool() {
        return type() != DBUS_TYPE_INVALID;
    }

    int type() {
        return m_type;
    }

    auto next() {
        m_DBus->message_iter_next(&m_Iter);
        // Resolve any variants
        m_resolved_iter = resolve_variants();
        m_type = m_DBus->message_iter_get_arg_type(&m_resolved_iter);
        return *this;
    }

    auto get_array_iter() {
        if(not is_array()) {
            std::cerr << "Not an array\n";
            return DBusMessageIter_wrap(DBusMessageIter{}, m_DBus);
        }

        DBusMessageIter ret;
        m_DBus->message_iter_recurse(&m_resolved_iter, &ret);
        return DBusMessageIter_wrap(ret, m_DBus);
    }

    auto get_dict_entry_iter() {
        if(type() != DBUS_TYPE_DICT_ENTRY){
            std::cerr << "Not a dict entry\n";
            return DBusMessageIter_wrap(DBusMessageIter{}, m_DBus);
        }

        DBusMessageIter ret;
        m_DBus->message_iter_recurse(&m_resolved_iter, &ret);
        return DBusMessageIter_wrap(ret, m_DBus);
    }

    auto get_stringified() -> std::string;

    template<class T>
    auto get_primitive() -> T;

    bool is_unsigned() {
        return (
            (type() == DBUS_TYPE_BYTE) ||
            (type() == DBUS_TYPE_INT16) ||
            (type() == DBUS_TYPE_INT32) ||
            (type() == DBUS_TYPE_INT64)
        );
    }

    bool is_signed() {
        return (
            (type() == DBUS_TYPE_INT16) ||
            (type() == DBUS_TYPE_INT32) ||
            (type() == DBUS_TYPE_INT64)
        );
    }

    bool is_string() {
        return (type() == DBUS_TYPE_STRING);
    }

    bool is_double() {
        return (type() == DBUS_TYPE_DOUBLE);
    }

    bool is_primitive() {
        return (
            is_double() ||
            is_signed() ||
            is_unsigned() ||
            is_string()
        );
    }

    bool is_array() {
        return (type() == DBUS_TYPE_ARRAY);
    }

    uint64_t get_unsigned() {
        auto t = type();
        switch (t)
        {
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

    uint64_t get_signed() {
        auto t = type();
        switch (t)
        {
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
};

template<class T>
auto DBusMessageIter_wrap::get_primitive() -> T {
    auto requested_type = dbus_type_identifier_v<T>;
    if(requested_type != type()){
        std::cerr << "Type mismatch: '" << (char) requested_type << "' vs '" << (char) type() << "'\n";
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

template<>
auto DBusMessageIter_wrap::get_primitive<std::string>() -> std::string {
    return std::string(get_primitive<const char*>());
}

auto DBusMessageIter_wrap::get_stringified() -> std::string {
    if(is_string()) return get_primitive<std::string>();
    if(is_unsigned()) return std::to_string(get_unsigned());
    if(is_signed()) return std::to_string(get_signed());
    if(is_double()) return std::to_string(get_primitive<double>());
    std::cerr << "stringify failed\n";
    return std::string();
}

// Precondition: iter points to a dict of string -> any
// executes action(key, value_iter) for all entries
template<class T>
void string_map_for_each(DBusMessageIter_wrap iter, T action) {
    iter = iter.get_array_iter();
    for(; iter; iter.next()) {
        auto it = iter.get_dict_entry_iter();
        auto key = it.get_primitive<std::string>();

        it.next();
        action(key, it);
    }
}

template<class T, class Callable>
void array_for_each(DBusMessageIter_wrap iter, Callable action) {
    iter = iter.get_array_iter();
    for(; iter; iter.next()){
        action(iter.get_primitive<T>());
    }
}

template<class Callable>
void array_for_each_stringify(DBusMessageIter_wrap iter, Callable action) {
    iter = iter.get_array_iter();
    for(; iter; iter.next()){
        action(iter.get_stringified());
    }
}

template<class T>
void string_multimap_for_each_stringify(DBusMessageIter_wrap iter, T action) {
    string_map_for_each(iter, [&](const std::string& key, DBusMessageIter_wrap it){
        if(it.is_array()){
            array_for_each_stringify(it, [&](const std::string& val){
                action(key, val);
            });
        }
        else if(it.is_primitive()){
            action(key, it.get_stringified());
        }
    });
}

class DBusMessage_wrap {
public:
    DBusMessage_wrap(DBusMessage* msg, libdbus_loader* ldr, bool owning = false)
        : m_owning(owning), m_msg(msg), m_DBus(ldr)
    {}

    ~DBusMessage_wrap(){
        free_if_owning();
    }

    DBusMessage_wrap(const DBusMessage_wrap&) = delete;
    DBusMessage_wrap(DBusMessage_wrap&&) = default;

    operator bool() const {
        return m_msg != nullptr;
    }

    template<class T>
    DBusMessage_wrap& argument(T arg) {
        if(not m_msg) return *this;
        if(not m_DBus->message_append_args(
            m_msg, 
            dbus_type_identifier_v<T>, 
            &arg,
            DBUS_TYPE_INVALID
        )){
            free_if_owning();
        }
        return *this;
    }

    DBusMessage_wrap send_with_reply_and_block(DBusConnection* conn) {
        if(not m_msg){
            return DBusMessage_wrap(nullptr, m_DBus);
        }
        DBusError err;
        m_DBus->error_init(&err);
        auto reply = m_DBus->connection_send_with_reply_and_block(
            conn,
            m_msg,
            DBUS_TIMEOUT,
            &err
        );
        if(reply == nullptr) {
            std::cerr << "MangoHud[" << __func__ << "]: " << err.message << "\n";
            free_if_owning();
            m_DBus->error_free(&err);
        }
        return DBusMessage_wrap(reply, m_DBus, true);
    }

    DBusMessageIter_wrap iter() {
        return DBusMessageIter_wrap(m_msg, m_DBus);
    }

    static DBusMessage_wrap new_method_call(
        const std::string& bus_name,
        const std::string& path,
        const std::string& iface,
        const std::string& method,
        libdbus_loader* loader
    ){
        auto msg = loader->message_new_method_call(
            (bus_name.empty()) ? nullptr : bus_name.c_str(),
            path.c_str(),
            (iface.empty()) ? nullptr : iface.c_str(),
            method.c_str()
        );
        return DBusMessage_wrap(msg, loader, true);
    }
private:
    void free_if_owning() {
        if(m_msg and m_owning) m_DBus->message_unref(m_msg);
        m_msg = nullptr;
    }
    bool m_owning;
    DBusMessage* m_msg;
    libdbus_loader* m_DBus;
    std::vector<std::string> m_args;
};

template<>
DBusMessage_wrap& DBusMessage_wrap::argument<const std::string&>(const std::string& str)
{
    return argument<const char*>(str.c_str());
}

template<class T>
static void assign_metadata_value(metadata& meta, const std::string& key, const T& value) {
    if(key == "PlaybackStatus") {
        meta.playing = (value == "Playing");
        meta.got_playback_data = true;
    }
    else if(key == "xesam:title"){
        meta.title = value;
        meta.got_song_data = true;
    }
    else if(key == "xesam:artist") {
        meta.artists = value;
        meta.got_song_data = true;
    }
    else if(key == "xesam:album") {
        meta.album = value;
        meta.got_song_data = true;
    }
    else if(key == "mpris:artUrl"){
        meta.artUrl = value;
        meta.got_song_data = true;
    }
}

std::string format_signal(const dbusmgr::DBusSignal& s)
{
    std::stringstream ss;
    ss << "type='signal',interface='" << s.intf << "'";
    ss << ",member='" << s.signal << "'";
    return ss.str();
}

static void parse_mpris_properties(libdbus_loader& dbus, DBusMessage *msg, std::string& source, metadata& meta)
{
    /**
     *  Expected response Format:
     *      string,
     *      map{
     *          "Metadata" -> multimap,
     *          "PlaybackStatus" -> string
     *      }
    */

    std::string key, val;
    auto iter = DBusMessageIter_wrap(msg, &dbus);

    // Should be 'org.mpris.MediaPlayer2.Player'
    if (not iter.is_string()){
        std::cerr << "Not a string\n";  //TODO
        return;
    }

    source = iter.get_primitive<std::string>();

    if (source != "org.mpris.MediaPlayer2.Player")
        return;

    iter.next();
    if (not iter.is_array())
        return;

    std::cerr << "Parsing mpris update...\n";
    string_map_for_each(iter, [&](std::string& key, DBusMessageIter_wrap it){
        if(key == "Metadata"){
            std::cerr << "\tMetadata:\n";
            string_map_for_each(it, [&](const std::string& key, DBusMessageIter_wrap it){
                if(it.is_primitive()){
                    auto val = it.get_stringified();
                    std::cerr << "\t\t" << key << " -> " << val << "\n";
                    assign_metadata_value(meta, key, val);
                }
                else if(it.is_array()){
                    std::string val;
                    array_for_each_stringify(it, [&](const std::string& str){
                        if(val.empty()){
                            val = str;
                        }
                        else {
                            val += ", " + str;
                        }
                    });
                    std::cerr << "\t\t" << key << " -> " << val << "\n";
                    assign_metadata_value(meta, key, val);
                }
            });
            string_multimap_for_each_stringify(it, [&](const std::string& key, const std::string& val){
                assign_metadata_value(meta, key, val);
            });
        }
        else if(key == "PlaybackStatus"){
            auto val = it.get_stringified();
            std::cerr << "\tPlaybackStatus:\n";
            std::cerr << "\t\t" << key << " -> " << val << "\n";
            assign_metadata_value(meta, key, val);
        }
    });
    meta.valid = (meta.artists.size() || !meta.title.empty());
}

bool dbus_get_name_owner(dbusmgr::dbus_manager& dbus_mgr, std::string& name_owner, const char *name)
{
    auto reply = DBusMessage_wrap::new_method_call(
        "org.freedesktop.DBus", 
        "/org/freedesktop/DBus", 
        "org.freedesktop.DBus", 
        "GetNameOwner",
        &dbus_mgr.dbus()
    ).argument(name).send_with_reply_and_block(dbus_mgr.get_conn());
    if(not reply) return false;

    auto iter = reply.iter();
    if(not iter.is_string()) return false;
    name_owner = iter.get_primitive<std::string>();
    return true;
}

bool dbus_get_player_property(dbusmgr::dbus_manager& dbus_mgr, metadata& meta, const char * dest, const char * prop)
{
    auto reply = DBusMessage_wrap::new_method_call(
        dest,
        "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties",
        "Get",
        &dbus_mgr.dbus()
    ).argument("org.mpris.MediaPlayer2.Player")
    .argument(prop)
    .send_with_reply_and_block(dbus_mgr.get_conn());

    if(not reply) return false;

    auto iter = reply.iter();
    if(iter.is_array()){
        string_multimap_for_each_stringify(iter, [&](const std::string& key, const std::string& val){
            assign_metadata_value(meta, key, val);
        });
    }
    else if(iter.is_primitive()){
        assign_metadata_value(meta, prop, iter.get_stringified());
    }
    else {
        return false;
    }
    return true;
}

namespace dbusmgr {
bool dbus_manager::get_media_player_metadata(metadata& meta, std::string name) {
    if(name == "") name = m_active_player;
    if(name == "") return false;
    meta.clear();
    dbus_get_player_property(*this, meta, name.c_str(), "Metadata");
    dbus_get_player_property(*this, meta, name.c_str(), "PlaybackStatus");
    meta.valid = (meta.artists.size() || !meta.title.empty());
    return true;
}

bool dbus_manager::init(const std::string& requested_player)
{
    if (m_inited)
        return true;
    
    m_requested_player = "org.mpris.MediaPlayer2." + requested_player;

    if (!m_dbus_ldr.IsLoaded() && !m_dbus_ldr.Load("libdbus-1.so.3")) {
        std::cerr << "MANGOHUD: Could not load libdbus-1.so.3\n";
        return false;
    }

    m_dbus_ldr.error_init(&m_error);

    m_dbus_ldr.threads_init_default();

    if ( nullptr == (m_dbus_conn = m_dbus_ldr.bus_get(DBUS_BUS_SESSION, &m_error)) ) {
        std::cerr << "MANGOHUD: " << m_error.message << std::endl;
        m_dbus_ldr.error_free(&m_error);
        return false;
    }

    std::cout << "MANGOHUD: Connected to D-Bus as \"" << m_dbus_ldr.bus_get_unique_name(m_dbus_conn) << "\"." << std::endl;

    dbus_list_name_to_owner();
    connect_to_signals();

    select_active_player();
    {
        std::lock_guard<std::mutex> lck(main_metadata.mtx);
        get_media_player_metadata(main_metadata.meta);
    }

    m_inited = true;
    return true;
}

bool dbus_manager::select_active_player() {
    // If the requested player is available, use it
    if(m_name_owners.count(m_requested_player) > 0) {
        m_active_player = m_requested_player;
        std::cerr << "Selecting requested player: " << m_requested_player << "\n";
        return true;
    }

    // Else, use any player that is currently playing..
    for(const auto& entry : m_name_owners) {
        const auto& name = std::get<0>(entry);
        metadata meta;
        get_media_player_metadata(meta, name);
        if(meta.playing) {
            m_active_player = name;
            std::cerr << "Selecting fallback player: " << name << "\n";
            return true;
        }
    }

    // No media players are active
    std::cerr << "No active players\n";
    m_active_player = "";
    return false;
}

void dbus_manager::deinit()
{
    if (!m_inited)
        return;

    // unreference system bus connection instead of closing it
    if (m_dbus_conn) {
        disconnect_from_signals();
        m_dbus_ldr.connection_unref(m_dbus_conn);
        m_dbus_conn = nullptr;
    }
    m_dbus_ldr.error_free(&m_error);
    m_inited = false;
}

dbus_manager::~dbus_manager()
{
    deinit();
}

DBusHandlerResult dbus_manager::filter_signals(DBusConnection* conn, DBusMessage* msg, void* userData) {
    auto& manager = *reinterpret_cast<dbus_manager*>(userData);

    for(auto& sig : manager.m_signals) {
        if(manager.m_dbus_ldr.message_is_signal(msg, sig.intf, sig.signal)){
            auto sender = manager.m_dbus_ldr.message_get_sender(msg);
            if((manager.*(sig.handler))(msg, sender)) 
                return DBUS_HANDLER_RESULT_HANDLED;
            else
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

bool dbus_manager::handle_properties_changed(DBusMessage* msg, const char* sender) {
    std::string source;

    metadata meta;
    parse_mpris_properties(m_dbus_ldr, msg, source, meta);
#ifndef NDEBUG
    std::cerr << "PropertiesChanged Signal received:\n";
    std::cerr << "\tSource: " << source << "\n";
    std::cerr << "active_player:         " << m_active_player << "\n";
    std::cerr << "active_player's owner: " << m_name_owners[m_active_player] << "\n";
    std::cerr << "sender:                " << sender << "\n";
#endif
    if (source != "org.mpris.MediaPlayer2.Player")
        return false;

    if(m_active_player == "") {
        select_active_player();
    }
    if (m_name_owners[m_active_player] == sender) {
        std::lock_guard<std::mutex> lck(main_metadata.mtx);
        if(meta.got_song_data){
            main_metadata.meta = meta;
            main_metadata.meta.playing = true;
        }
        if(meta.got_playback_data){
            main_metadata.meta.playing = meta.playing;
        }
    }
    return true;
}

bool dbus_manager::handle_name_owner_changed(DBusMessage* msg, const char* sender) {
    DBusMessageIter iter;
    m_dbus_ldr.message_iter_init (msg, &iter);
    std::vector<std::string> str;
    const char *value = nullptr;

    while (m_dbus_ldr.message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING) {
        m_dbus_ldr.message_iter_get_basic (&iter, &value);
        str.push_back(value);
        m_dbus_ldr.message_iter_next (&iter);
    }

    // register new name
    if (str.size() == 3
        && starts_with(str[0], "org.mpris.MediaPlayer2.")
        && !str[2].empty()
    )
    {
        m_name_owners[str[0]] = str[2];
        if(str[0] == m_requested_player){
            select_active_player();
            metadata tmp;
            get_media_player_metadata(tmp);
            {
                std::lock_guard<std::mutex> lck(main_metadata.mtx);
                main_metadata.meta = tmp;
            }
        }
    }

    // did a player quit?
    if (str[2].empty()) {
        if (str.size() == 3
            && str[0] == m_active_player
        ) {
            metadata tmp;
            m_name_owners.erase(str[0]);
            select_active_player();
            get_media_player_metadata(tmp);
            {
                std::lock_guard<std::mutex> lck(main_metadata.mtx);
                std::swap(tmp, main_metadata.meta);
            }
        }
    }
    return true;
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
    m_dbus_ldr.connection_add_filter(m_dbus_conn, filter_signals, reinterpret_cast<void*>(this), nullptr);

    start_thread();
}

void dbus_manager::disconnect_from_signals()
{
    m_dbus_ldr.connection_remove_filter(m_dbus_conn, filter_signals, reinterpret_cast<void*>(this));
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

bool dbus_manager::dbus_list_name_to_owner()
{
    DBusError error;

    std::vector<std::string> names;
    std::string owner;

    DBusMessage * dbus_reply = nullptr;
    DBusMessage * dbus_msg = nullptr;

    // dbus-send --session --dest=org.freedesktop.DBus --type=method_call --print-reply /org/freedesktop/DBus org.freedesktop.DBus.GetNameOwner string:"org.mpris.MediaPlayer2.spotify"
    if (nullptr == (dbus_msg = m_dbus_ldr.message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames"))) {
        std::cerr << "MANGOHUD: " << __func__ << ": unable to allocate memory for dbus message\n";
        return false;
    }

    m_dbus_ldr.error_init(&error);
    if (nullptr == (dbus_reply = m_dbus_ldr.connection_send_with_reply_and_block(dbus_mgr.get_conn(), dbus_msg, DBUS_TIMEOUT, &error))) {
        m_dbus_ldr.message_unref(dbus_msg);
        std::cerr << "MANGOHUD: " << __func__ << ": "<< error.message << "\n";
        m_dbus_ldr.error_free(&error);
        return false;
    }

    auto iter = DBusMessageIter_wrap(dbus_reply, &m_dbus_ldr);
    if(not iter.is_array()) {
        m_dbus_ldr.message_unref(dbus_msg);
        m_dbus_ldr.message_unref(dbus_reply);
        m_dbus_ldr.error_free(&error);
        return false;
    }
    array_for_each<std::string>(iter, [&](std::string name){
        if(!starts_with(name.c_str(), "org.mpris.MediaPlayer2.")) return;
        if(dbus_get_name_owner(dbus_mgr, owner, name.c_str())){
            m_name_owners[name] = owner;
        }
    });

    m_dbus_ldr.message_unref(dbus_msg);
    m_dbus_ldr.message_unref(dbus_reply);
    m_dbus_ldr.error_free(&error);
    return true;
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
    m_thread = std::thread(&dbus_manager::dbus_thread, this);
}

void dbus_manager::dbus_thread()
{
    using namespace std::chrono_literals;
    while(!m_quit && m_dbus_ldr.connection_read_write_dispatch(m_dbus_conn, 0))
        std::this_thread::sleep_for(10ms);
}

}
