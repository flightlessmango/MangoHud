#pragma once
#include <stdint.h>
#include <string>
#include <variant>
#include <vector>
#include <optional>
#include <memory>
#include "imgui.h"
#include <deque>

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

enum class CellAlign {
    Default,
    Left,
    Center,
    Right,
};

struct CellStyle {
    float padding = -1.0f;
    bool separator = false;
    std::string separator_text;
    std::string separator_color = "FFFFFF";
    ImVec4 separator_vec = {1.0f, 1.0f, 1.0f, 1.0f};
    float separator_thickness = 1.0f;
    std::string background_color;
    ImVec4 background_vec = {0.0f, 0.0f, 0.0f, 0.0f};
    float font_size = 0.0f;
    float font_scale = 1.0f;
    int colspan = 1;
    int truncate = 0;
    int value_reserve = 4;
    bool value_reserve_override = false;
    CellAlign align = CellAlign::Default;
};

struct TextCell {
    std::string text;
    std::string color;
    ImVec4 vec;
    std::string unit;
    std::vector<float> data;
    CellStyle style;
};

struct ValueCell {
    MetricRef ref;
    std::string unit;
    bool unit_override = false;
    std::string color;
    int precision = 1;
    CellStyle style;
};

struct FormatCell {
    std::string format;
    std::string unit;
    std::vector<MetricRef> values;
    std::string color;
    CellStyle style;
};

struct GraphCell {
    MetricRef ref;
    std::string graph;
    int width = 0;
    int height = 0;
    int min = 0;
    int max = 0;
    uint32_t color;
    CellStyle style;
};

using ProgressBound = std::variant<float, MetricRef>;

struct ProgressCell {
    MetricRef ref;
    ProgressBound min = 0.0f;
    ProgressBound max = 100.0f;
    float value = 0.0f;
    float min_value = 0.0f;
    float max_value = 100.0f;
    std::string text;
    std::string layout_text;
    std::string unit;
    bool unit_override = false;
    std::string color;
    std::string background_color;
    ImVec4 vec;
    ImVec4 background_vec;
    int precision = 1;
    CellStyle style;
};

struct ExecCell {
    std::string command;
    std::string unit;
    std::string color;
    CellStyle style;
};

struct hudTable;

struct TableCell {
    std::shared_ptr<hudTable> table;
    CellStyle style;
};

using Cell = std::variant<TextCell, ValueCell, FormatCell, GraphCell, ProgressCell, ExecCell, TableCell>;
using MaybeCell = std::optional<Cell>;

struct hudTable {
    float padding = 0.0f;
    int cols = 0;
    int font_size = 24;
    float col_gap = 8.0f;
    float row_gap = -2.0f;
    bool debug_cell_boxes = false;
    std::vector<std::vector<MaybeCell>> rows;
};

struct HudWindow {
    bool background = true;
    ImVec2 position = {10.0f, 10.0f};
    hudTable table;
};

struct HudConfig {
    std::vector<HudWindow> windows;

    HudConfig() {
        windows.emplace_back();
    }

    HudWindow& default_window() {
        return windows.front();
    }

    const HudWindow& default_window() const {
        return windows.front();
    }
};
