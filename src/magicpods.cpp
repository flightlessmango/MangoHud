#include "magicpods.h"

#include "overlay_params.h"
#include "ws_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#ifdef __linux__
#include <pthread.h>
#endif

namespace {
using json = nlohmann::json;
using namespace std::chrono_literals;

class MagicPodsService {
public:
   ~MagicPodsService()
   {
      stop();
   }

   void ensure(const overlay_params& params)
   {
      bool wanted = std::find(params.device_battery.begin(), params.device_battery.end(), "headset") != params.device_battery.end();
      if (!wanted) {
         stop();
         return;
      }

      const std::string url = params.magicpods_url.empty() ? "127.0.0.1:2020" : params.magicpods_url;
      {
         std::lock_guard<std::mutex> lock(m_thread_mutex);
         if (m_running && m_url == url)
            return;
      }

      stop();

      std::lock_guard<std::mutex> lock(m_thread_mutex);
      m_url = url;
      m_stop.store(false);
      m_running = true;
      m_thread = std::thread(&MagicPodsService::run, this);
#ifdef __linux__
      pthread_setname_np(m_thread.native_handle(), "mangohud-magicp");
#endif
   }

   void stop()
   {
      {
         std::lock_guard<std::mutex> lock(m_thread_mutex);
         if (!m_running)
            return;
         m_stop.store(true);
         m_cv.notify_all();
      }

      if (m_thread.joinable())
         m_thread.join();

      {
         std::lock_guard<std::mutex> lock(m_thread_mutex);
         m_running = false;
      }
      set_snapshot({});
   }

   headset_battery snapshot()
   {
      std::lock_guard<std::mutex> lock(m_snapshot_mutex);
      return m_snapshot;
   }

private:
   struct endpoint {
      std::string host = "127.0.0.1";
      std::string port = "2020";
      std::string path = "/";
   };

   static endpoint parse_url(std::string url)
   {
      endpoint ep;
      const std::string scheme = "ws://";
      if (url.rfind(scheme, 0) == 0)
         url.erase(0, scheme.size());

      auto slash = url.find('/');
      std::string hostport = slash == std::string::npos ? url : url.substr(0, slash);
      ep.path = slash == std::string::npos ? "/" : url.substr(slash);
      if (ep.path.empty())
         ep.path = "/";

      if (!hostport.empty() && hostport.front() == '[') {
         auto close = hostport.find(']');
         if (close != std::string::npos) {
            ep.host = hostport.substr(1, close - 1);
            if (close + 2 <= hostport.size() && hostport[close + 1] == ':')
               ep.port = hostport.substr(close + 2);
            return ep;
         }
      }

      auto colon = hostport.rfind(':');
      if (colon != std::string::npos) {
         ep.host = hostport.substr(0, colon);
         ep.port = hostport.substr(colon + 1);
      } else if (!hostport.empty()) {
         ep.host = hostport;
      }
      if (ep.host.empty()) ep.host = "127.0.0.1";
      if (ep.port.empty()) ep.port = "2020";
      return ep;
   }

   void set_snapshot(const headset_battery& value)
   {
      std::lock_guard<std::mutex> lock(m_snapshot_mutex);
      m_snapshot = value;
   }

   bool wait_backoff(std::chrono::seconds delay)
   {
      std::unique_lock<std::mutex> lock(m_wait_mutex);
      return m_cv.wait_for(lock, delay, [&] { return m_stop.load(); });
   }

   void run()
   {
      std::chrono::seconds backoff = 1s;
      bool logged_down = false;

      while (!m_stop.load()) {
         auto ep = parse_url(m_url);
         WebSocketClient ws;
         if (!ws.connect(ep.host, ep.port, ep.path, 1000ms)) {
            if (!logged_down) {
               SPDLOG_INFO("MagicPodsCore WebSocket unavailable at {}:{}", ep.host, ep.port);
               logged_down = true;
            }
            if (wait_backoff(backoff))
               break;
            backoff = std::min(backoff * 2, 30s);
            continue;
         }

         SPDLOG_INFO("Connected to MagicPodsCore WebSocket at {}:{}", ep.host, ep.port);
         logged_down = false;
         backoff = 1s;
         set_snapshot({});
         ws.send_text("{\"method\":\"GetAll\"}");

         while (!m_stop.load()) {
            std::string msg;
            auto status = ws.recv_message(msg, 1000ms);
            if (status == WebSocketClient::RecvStatus::Timeout)
               continue;
            if (status != WebSocketClient::RecvStatus::Message) {
               SPDLOG_DEBUG("MagicPodsCore WebSocket disconnected");
               set_snapshot({});
               break;
            }

            headset_battery next = snapshot();
            if (MagicPods::parse_message(msg, next))
               set_snapshot(next);
         }
      }
   }

   std::mutex m_thread_mutex;
   std::thread m_thread;
   bool m_running = false;
   std::atomic<bool> m_stop {false};
   std::condition_variable m_cv;
   std::mutex m_wait_mutex;
   std::string m_url = "127.0.0.1:2020";

   std::mutex m_snapshot_mutex;
   headset_battery m_snapshot;
};

MagicPodsService& service()
{
   static MagicPodsService instance;
   return instance;
}

std::string sanitize_name(const json& value)
{
   std::string name = value.is_string() ? value.get<std::string>() : "HEADSET";
   std::string out;
   out.reserve(std::min<size_t>(name.size(), 24));
   for (unsigned char c : name) {
      if (out.size() >= 24)
         break;
      if (!std::isprint(c))
         continue;
      out.push_back(static_cast<char>(std::toupper(c)));
   }
   return out.empty() ? "HEADSET" : out;
}

headset_slot parse_slot(const json& root, const char* key)
{
   headset_slot slot;
   if (!root.contains(key) || !root[key].is_object())
      return slot;
   const auto& obj = root[key];
   int battery = obj.value("battery", -1);
   int status = obj.value("status", 0);
   slot.battery = static_cast<int8_t>(std::clamp(battery, 0, 100));
   slot.status = static_cast<uint8_t>((status >= 0 && status <= 3) ? status : 0);
   slot.charging = obj.value("charging", false);
   return slot;
}

bool visible(const headset_slot& slot)
{
   return (slot.status == 2 || slot.status == 3) && slot.battery >= 0;
}

bool any_visible(const headset_battery& battery)
{
   return visible(battery.single) || visible(battery.left) ||
          visible(battery.right) || visible(battery.charging_case);
}

void parse_info(const json& info, headset_battery& battery)
{
   if (!info.is_object() || info.empty() || !info.value("connected", false)) {
      battery = {};
      return;
   }

   if (!info.contains("capabilities") ||
       !info["capabilities"].contains("battery") ||
       !info["capabilities"]["battery"].is_object()) {
      battery = {};
      return;
   }

   headset_battery parsed;
   parsed.name = sanitize_name(info.value("name", json("HEADSET")));
   const auto& slots = info["capabilities"]["battery"];
   parsed.single = parse_slot(slots, "single");
   parsed.left = parse_slot(slots, "left");
   parsed.right = parse_slot(slots, "right");
   parsed.charging_case = parse_slot(slots, "case");
   parsed.valid = any_visible(parsed);
   battery = parsed;
}
}

void MagicPods::ensure(const overlay_params& params)
{
   service().ensure(params);
}

headset_battery MagicPods::snapshot()
{
   return service().snapshot();
}

void MagicPods::stop()
{
   service().stop();
}

bool MagicPods::parse_message(const std::string& message, headset_battery& battery)
{
   if (message.size() > 64 * 1024)
      return false;

   json root = json::parse(message, nullptr, false);
   if (root.is_discarded() || !root.is_object())
      return false;

   if (root.contains("init") && root["init"].is_object()) {
      int api = root["init"].value("api", 0);
      std::string version = root["init"].value("version", "");
      SPDLOG_DEBUG("MagicPodsCore API {} version {}", api, version);
      if (api != 0)
         SPDLOG_WARN("MagicPodsCore API version {} differs from supported API 0", api);
   }

   if (root.contains("defaultbluetooth") && root["defaultbluetooth"].is_object() &&
       root["defaultbluetooth"].value("enabled", true) == false) {
      battery = {};
      return true;
   }

   if (root.contains("info")) {
      parse_info(root["info"], battery);
      return true;
   }

   return false;
}
