#include "magicpods.h"

#include <cassert>
#include <string>

static headset_battery parse(const std::string& msg)
{
   headset_battery battery;
   bool changed = MagicPods::parse_message(msg, battery);
   assert(changed);
   return battery;
}

int main()
{
   auto tws = parse(R"json({
      "info": {
         "name": "AirPods Pro\u0001 Extra Long Headphone Name",
         "connected": true,
         "capabilities": {
            "battery": {
               "single": {"battery": 0, "charging": false, "status": 0},
               "left": {"battery": 80, "charging": true, "status": 2},
               "right": {"battery": 70, "charging": false, "status": 2},
               "case": {"battery": 250, "charging": false, "status": 3}
            }
         }
      }
   })json");
   assert(tws.valid);
   assert(tws.name == "AIRPODS PRO EXTRA LONG H");
   assert(tws.left.battery == 80);
   assert(tws.left.charging);
   assert(tws.right.battery == 70);
   assert(tws.charging_case.battery == 100);
   assert(!tws.single.status);

   auto single = parse(R"json({
      "info": {
         "name": "Beats Flex",
         "connected": true,
         "capabilities": {
            "battery": {
               "single": {"battery": 95, "charging": false, "status": 2},
               "left": {"battery": 0, "charging": false, "status": 0},
               "right": {"battery": 0, "charging": false, "status": 0},
               "case": {"battery": 0, "charging": false, "status": 0}
            }
         }
      }
   })json");
   assert(single.valid);
   assert(single.name == "BEATS FLEX");
   assert(single.single.battery == 95);

   auto missing_caps = parse(R"json({"info":{"name":"buds","connected":true}})json");
   assert(!missing_caps.valid);

   auto disconnected = parse(R"json({"info":{}})json");
   assert(!disconnected.valid);

   headset_battery existing = single;
   assert(MagicPods::parse_message(R"json({"defaultbluetooth":{"enabled":false}})json", existing));
   assert(!existing.valid);

   existing = single;
   assert(!MagicPods::parse_message("", existing));
   assert(existing.valid);

   auto hidden = parse(R"json({
      "info": {
         "name": "Hidden",
         "connected": true,
         "capabilities": {
            "battery": {
               "single": {"battery": 50, "charging": false, "status": 9},
               "left": {"battery": 50, "charging": false, "status": 1},
               "right": {"battery": 50, "charging": false, "status": 0},
               "case": {"battery": 50, "charging": false, "status": 0}
            }
         }
      }
   })json");
   assert(!hidden.valid);

   return 0;
}
