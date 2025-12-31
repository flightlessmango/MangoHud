#pragma once
#include <string>
#include <optional>
#include <variant>
#include <vector>
#include <unordered_set>
#include <inttypes.h>
#include <charconv>
#include <memory>
#include "common/table_structs.h"

std::unique_ptr<HudTable> parse_hud_table(const char* file);

struct TokenParts {
    std::string_view prefix;
    std::string_view suffix;
    int index = 0;
    bool has_index = false;
};

std::optional<TokenParts> splitToken(std::string_view s);
// std::optional<MetricRef> metricRefFromString(std::string_view token);

// static float resolveMetric(const MetricRef& r) {
//     switch (r.id) {
//         case MetricId::GPU_LOAD:    return 0.f; // TODO
//         case MetricId::GPU_TEMP:    return 0.f; // TODO
//         case MetricId::GPU_CLOCK:   return 0.f; // TODO
//         case MetricId::GPU_POWER:   return 0.f; // TODO
//         case MetricId::VRAM_USED:   return 0.f; // TODO
//         case MetricId::VRAM_CLOCK:  return 0.f; // TODO
//         case MetricId::CPU_LOAD:    return 0.f; // TODO
//         case MetricId::CPU_CLOCK:   return 0.f; // TODO
//         case MetricId::CPU_TEMP:    return 0.f; // TODO
//         case MetricId::RAM_USED:    return 0.f;  // TODO
//         case MetricId::FPS:         return 0.f; // TODO
//         case MetricId::FRAMETIME:   return 0.f; // TODO
//     }

//     return 0.f;
// }
