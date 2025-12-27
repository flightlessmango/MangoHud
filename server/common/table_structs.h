#pragma once
#include <stdint.h>
#include <string>
#include <variant>
#include <vector>
#include <optional>
#include "imgui.h"

enum class MetricId : uint16_t {
  GPU_LOAD, GPU_TEMP, GPU_CLOCK, GPU_POWER,
  VRAM_USED, VRAM_CLOCK,
  CPU_LOAD, CPU_TEMP, CPU_CLOCK,
  RAM_USED,
  FPS, FRAMETIME,
};

struct MetricRef {
    std::string a;
    std::string b;
};

struct TextCell {
    std::string text;
    std::string color;
    ImVec4 vec;
    std::string unit;
    std::vector<float> data;
};

struct ValueCell {
    MetricRef ref;
    std::string unit;
    std::string color;
    int precision;
};

struct GraphCell {
    MetricRef ref;
    std::string graph;
    int width = 0;
    int height = 0;
    int min = 0;
    int max = 0;
    uint32_t color;
};

using Cell = std::variant<TextCell, ValueCell, GraphCell>;
using MaybeCell = std::optional<Cell>;

struct HudTable {
    int cols = 0;
    int font_size = 24;
    std::vector<std::vector<MaybeCell>> rows;
};
