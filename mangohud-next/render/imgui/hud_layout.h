#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <vector>
#include "imgui_internal.h"
#include "../../server/common/table_structs.h"

class Font;

inline constexpr float hud_outline = 1.5f;

struct HudText {
    std::string_view text;
    ImFont* font = nullptr;
    float font_size = 0.0f;
    ImVec2 pos{};
    ImVec4 color{};
};

struct HudLayout;

struct HudCellLayout {
    const Cell* cell = nullptr;
    ImRect bounds{0, 0, 0, 0};
    ImRect content{0, 0, 0, 0};
    std::vector<HudText> text;
    ImRect progress{0, 0, 0, 0};
    std::optional<ImRect> separator;
    std::unique_ptr<HudLayout> nested;
};

struct HudRowLayout {
    ImRect bounds{0, 0, 0, 0};
    std::vector<HudCellLayout> cells;
};

struct HudLayout {
    ImVec2 size{};
    std::vector<HudRowLayout> rows;
    bool debug = false;
};

const CellStyle& hud_cell_style(const Cell& cell);
void prepare_hud_fonts(const hudTable& table, Font& fonts);
HudLayout layout_hud(const hudTable& table, Font& fonts);
