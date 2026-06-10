#pragma once

#include <cstdint>
#include <string>

struct overlay_params;

struct headset_slot {
   int8_t battery = -1;
   bool charging = false;
   uint8_t status = 0;
};

struct headset_battery {
   bool valid = false;
   std::string name;
   headset_slot single;
   headset_slot left;
   headset_slot right;
   headset_slot charging_case;
};

class MagicPods {
public:
   static void ensure(const overlay_params& params);
   static headset_battery snapshot();
   static void stop();

   static bool parse_message(const std::string& message, headset_battery& battery);
};
