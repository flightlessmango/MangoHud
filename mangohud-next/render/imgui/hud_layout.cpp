#define IMGUI_DEFINE_MATH_OPERATORS
#include "hud_layout.h"
#include "font/font.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cfloat>

constexpr float outline_space = 2.0f;
constexpr float unit_gap = 1.0f;

static ImRect glyph_bounds(std::string_view text, ImFont* font, float size) {
    ImRect bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
    const float scale = size / font->FontSize;
    float x = 0.0f, y = 0.0f;
    const char* p = text.data();
    const char* end = p + text.size();
    while (p < end) {
        unsigned int c = 0;
        const int bytes = ImTextCharFromUtf8(&c, p, end);
        if (bytes <= 0)
            break;
        p += bytes;
        if (c == '\n') {
            x = 0.0f;
            y += size;
            continue;
        }
        const auto* glyph = font->FindGlyph(static_cast<ImWchar>(c));
        if (!glyph)
            continue;
        if (glyph->Visible)
            bounds.Add(ImRect(x + glyph->X0 * scale, y + glyph->Y0 * scale, x + glyph->X1 * scale,
                              y + glyph->Y1 * scale));
        x += glyph->AdvanceX * scale;
    }
    return bounds.IsInverted() ? ImRect(0, 0, x, 0) : bounds;
}

static ImRect font_bounds(ImFont* font, float size) {
    return glyph_bounds("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZacefhiklmnrstuvwxz", font, size);
}

static float advance(std::string_view text, ImFont* font, float size) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
}

static ImRect text_bounds(std::string_view text, ImFont* font, float size) {
    auto bounds = glyph_bounds(text, font, size);
    const auto first = text.find_first_not_of(' ');
    if (first != std::string_view::npos) {
        bounds.Min.x -= advance(text.substr(0, first), font, size);
        bounds.Max.x += advance(text.substr(text.find_last_not_of(' ') + 1), font, size);
    }
    return bounds;
}

struct MeasuredCell {
    int column = 0, span = 1;
    float padding = 0.0f, left = 0.0f, right = 0.0f;
    ImVec2 minimum{}, text_size{};
    float value_width = 0.0f, unit_width = 0.0f;
    bool has_content = false, numeric = false;
    HudCellLayout layout;
};

static MeasuredCell measure_cell(const Cell& cell, int column, int cols, const hudTable& table,
                                 Font& fonts, const std::vector<bool>& reserve_overrides) {
    MeasuredCell measured;
    measured.column = column;
    const auto& style = hud_cell_style(cell);
    const float size =
        style.font_size > 0.0f ? style.font_size : table.font_size * style.font_scale;
    measured.span = std::min(std::max(1, style.colspan), cols - column);
    measured.padding = style.padding >= 0.0f ? style.padding : table.padding;
    measured.layout.cell = &cell;
    measured.has_content = !style.background_color.empty() || style.separator ||
                           !style.separator_text.empty();
    if (!style.separator_text.empty() && column + measured.span < cols) {
        auto* font = fonts.get(size);
        const auto bounds = text_bounds(style.separator_text, font, size);
        measured.layout.separator_text = HudText{style.separator_text, font, size,
                                                 {-bounds.Min.x, -bounds.Min.y},
                                                 style.separator_vec};
    }
    if (style.separator) {
        const float height = font_bounds(fonts.get(size), size).GetHeight();
        measured.layout.separator = ImRect(0, 0, 0, height);
    }
    if (const auto* text = std::get_if<TextCell>(&cell)) {
        measured.has_content |= !text->text.empty() || !text->data.empty();
        if (!text->data.empty())
            measured.minimum = {100.0f, 50.0f};
        else {
            auto* font = fonts.get(size);
            const auto reference = font_bounds(font, size);
            const auto value = text_bounds(text->text, font, size);
            measured.value_width =
                text->text.empty() ? 0.0f : value.GetWidth() + outline_space * 2.0f;
            measured.text_size = {measured.value_width,
                                  reference.GetHeight() + outline_space * 2.0f};
            auto& runs = measured.layout.text;
            runs.push_back({text->text,
                            font,
                            size,
                            {outline_space - value.Min.x, outline_space - reference.Min.y},
                            text->vec});
            if (!text->unit.empty()) {
                const float unit_size = text->unit == "%" ? size : size / 2.0f;
                auto* unit_font = fonts.get(unit_size);
                const auto unit = text_bounds(text->unit, unit_font, unit_size);
                const auto unit_reference = font_bounds(unit_font, unit_size);
                measured.unit_width = unit.GetWidth();
                const auto pos = ImVec2(runs[0].pos.x + value.Max.x + unit_gap - unit.Min.x,
                                        runs[0].pos.y + glyph_bounds(text->text, font, size).Min.y -
                                            unit_reference.Min.y);
                runs.push_back({text->unit, unit_font, unit_size, pos, {1, 1, 1, 1}});
                measured.text_size.x = pos.x + unit.Max.x + outline_space;
                measured.text_size.y =
                    std::max(measured.text_size.y, pos.y + unit_reference.Max.y + outline_space);
            }
            measured.numeric = !text->unit.empty() ||
                               (text->text.find_first_of("0123456789") != std::string::npos &&
                                text->text.find_first_not_of("0123456789.+-") == std::string::npos);
            const bool reserve_value = style.value_reserve_override ||
                                       (!reserve_overrides[column + measured.span - 1] &&
                                        (!text->unit.empty() || (column > 0 && measured.numeric)));
            const int reserve = std::max(reserve_value ? style.value_reserve : 0, style.truncate);
            if (reserve > 0)
                measured.value_width = std::max(measured.value_width,
                                               advance("0", font, size) * reserve +
                                                   outline_space * 2.0f);
            measured.minimum = measured.text_size;
            measured.minimum.x =
                std::max(measured.minimum.x,
                         measured.value_width +
                             (text->unit.empty() ? 0.0f : unit_gap + measured.unit_width));
        }
    } else if (const auto* progress = std::get_if<ProgressCell>(&cell)) {
        auto measure = [&](std::string_view text, bool emit) {
            float x = outline_space;
            auto append = [&](std::string_view run, float size) {
                auto* font = fonts.get(size);
                if (emit)
                    measured.layout.text.push_back(
                        {run,
                         font,
                         size,
                         {x, outline_space - font_bounds(font, size).Min.y},
                         {1, 1, 1, 1}});
                x += advance(run, font, size);
            };
            for (;;) {
                const auto unit = text.find("{unit}");
                append(text.substr(0, unit), size);
                if (unit == std::string_view::npos)
                    break;
                append(progress->unit, progress->unit == "%" ? size : size / 2.0f);
                text.remove_prefix(unit + 6);
            }
            return x + outline_space;
        };
        measured.has_content = true;
        measured.text_size = {measure(progress->text, true),
                              font_bounds(fonts.get(size), size).GetHeight() +
                                  outline_space * 2.0f};
        measured.minimum = {
            std::max({100.0f, measured.text_size.x, measure(progress->layout_text, false)}),
            measured.text_size.y};
        if (progress->text.empty() && progress->layout_text.empty())
            measured.minimum.y = std::max(6.0f, table.font_size * 0.6f);
    } else if (const auto* nested = std::get_if<TableCell>(&cell); nested && nested->table) {
        measured.layout.nested = std::make_unique<HudLayout>(layout_hud(*nested->table, fonts));
        measured.minimum = measured.layout.nested->size;
        for (const auto& row : measured.layout.nested->rows)
            measured.has_content |= row.bounds.GetWidth() > 0.0f;
    }
    if (measured.layout.separator_text) {
        const auto& run = *measured.layout.separator_text;
        measured.minimum.y = std::max(measured.minimum.y,
                                      text_bounds(run.text, run.font, run.font_size).GetHeight() +
                                          outline_space * 2.0f);
    }
    return measured;
}

static void position_text(MeasuredCell& cell, float unit_slot = 0.0f) {
    auto& layout = cell.layout;
    const auto& style = hud_cell_style(*layout.cell);
    CellAlign align = style.align;
    const bool progress = std::holds_alternative<ProgressCell>(*layout.cell);
    if (align == CellAlign::Default)
        align = progress           ? CellAlign::Center
                : cell.column == 0 ? CellAlign::Left
                                   : CellAlign::Right;
    float x = layout.content.Min.x;
    if (align == CellAlign::Right)
        x += layout.content.GetWidth() - cell.text_size.x;
    if (align == CellAlign::Center)
        x += (layout.content.GetWidth() - cell.text_size.x) * 0.5f;
    const float y = layout.content.Min.y + (layout.content.GetHeight() - cell.text_size.y) * 0.5f;
    for (auto& run : layout.text)
        run.pos += ImVec2(x, y);
    if (align == CellAlign::Right && cell.numeric) {
        auto& value = layout.text[0];
        const auto bounds = text_bounds(value.text, value.font, value.font_size);
        const float right =
            layout.content.Max.x - outline_space - (unit_slot > 0.0f ? unit_slot + unit_gap : 0.0f);
        value.pos.x = right - bounds.Max.x;
        if (layout.text.size() == 2) {
            auto& unit = layout.text[1];
            const auto unit_bounds = text_bounds(unit.text, unit.font, unit.font_size);
            unit.pos.x = right + unit_gap - unit_bounds.Min.x;
        }
    }
    if (align == CellAlign::Left)
        for (auto& run : layout.text)
            run.pos.x = std::ceil(run.pos.x);
}

const CellStyle& hud_cell_style(const Cell& cell) {
    return *std::visit([](const auto& value) -> const CellStyle* { return &value.style; }, cell);
}

void prepare_hud_fonts(const hudTable& table, Font& fonts) {
    for (const auto& row : table.rows)
        for (const auto& cell : row) {
            if (!cell)
                continue;
            const auto& style = hud_cell_style(*cell);
            const float size =
                style.font_size > 0.0f ? style.font_size : table.font_size * style.font_scale;
            fonts.get(size);
            fonts.get(size / 2.0f);
            if (const auto* nested = std::get_if<TableCell>(&*cell); nested && nested->table)
                prepare_hud_fonts(*nested->table, fonts);
        }
}

HudLayout layout_hud(const hudTable& table, Font& fonts) {
    HudLayout result;
    result.debug = table.debug_cell_boxes;
    const int cols = std::max(0, table.cols);
    std::vector<bool> reserve_overrides(cols);
    for (const auto& row : table.rows)
        for (int c = 0; c < cols && c < static_cast<int>(row.size()); c++) {
            if (!row[c])
                continue;
            const auto& style = hud_cell_style(*row[c]);
            if (style.value_reserve_override)
                reserve_overrides[c + std::min(std::max(1, style.colspan), cols - c) - 1] = true;
        }
    std::vector<std::vector<MeasuredCell>> rows;
    std::vector<float> gaps(cols, table.col_gap);
    std::vector<float> widths(cols), heights, unit_widths(cols), value_widths(cols),
        column_insets(cols);
    for (const auto& row : table.rows) {
        auto& cells = rows.emplace_back();
        float height = 0.0f, left = 0.0f;
        for (int c = 0; c < cols && c < static_cast<int>(row.size());) {
            if (!row[c]) {
                left = 0.0f;
                ++c;
                continue;
            }
            auto cell = measure_cell(*row[c], c, cols, table, fonts, reserve_overrides);
            if (cell.layout.separator_text) {
                const auto& run = *cell.layout.separator_text;
                auto& gap = gaps[c + cell.span - 1];
                gap = std::max(gap, text_bounds(run.text, run.font, run.font_size).GetWidth() +
                                        outline_space * 2.0f);
            }
            cell.left = left;
            const auto& style = hud_cell_style(*cell.layout.cell);
            left = 0.0f;
            if (style.separator) {
                const float half_width =
                    (style.separator_thickness + hud_outline * 2.0f + 1.0f) * 0.5f;
                cell.right = std::ceil(half_width + 0.5f);
                left = std::ceil(std::max(0.0f, half_width - 0.5f - table.col_gap));
            }
            const float inset = cell.padding * 2.0f + cell.left + cell.right;
            height = std::max(height, cell.minimum.y + cell.padding * 2.0f);
            if (cell.span == 1) {
                widths[c] = std::max(widths[c], cell.minimum.x + inset);
                column_insets[c] = std::max(column_insets[c], inset);
                if (cell.numeric)
                    value_widths[c] = std::max(value_widths[c], cell.value_width);
            }
            if (cell.span == 1 || style.align == CellAlign::Right ||
                (style.align == CellAlign::Default && c > 0))
                unit_widths[c + cell.span - 1] =
                    std::max(unit_widths[c + cell.span - 1], cell.unit_width);
            c += cell.span;
            cells.push_back(std::move(cell));
        }
        heights.push_back(height);
    }
    for (int c = 0; c < cols; c++)
        if (unit_widths[c] > 0.0f)
            widths[c] =
                std::max(widths[c], value_widths[c] + unit_gap + unit_widths[c] + column_insets[c]);
    for (int span = 2; span <= cols; span++)
        for (auto& row : rows)
            for (auto& cell : row) {
                if (cell.span != span)
                    continue;
                float available = 0.0f;
                for (int c = cell.column; c < cell.column + span; c++)
                    available += widths[c] + (c + 1 < cell.column + span ? gaps[c] : 0.0f);
                const auto align = hud_cell_style(*cell.layout.cell).align;
                const float unit = unit_widths[cell.column + span - 1];
                float minimum = cell.minimum.x;
                if (cell.numeric &&
                    (align == CellAlign::Right || (align == CellAlign::Default && cell.column > 0)))
                    minimum = std::max(minimum,
                                       cell.value_width + (unit > 0.0f ? unit_gap + unit : 0.0f));
                const float required = minimum + cell.padding * 2.0f + cell.left + cell.right;
                widths[cell.column + span - 1] += std::max(0.0f, required - available);
            }
    std::vector<float> x(cols + 1, 0.0f);
    for (int c = 0; c < cols; c++)
        x[c + 1] = x[c] + widths[c] + gaps[c];
    result.size.x = cols ? x.back() - gaps.back() : 0.0f;
    float y = 0.0f;
    for (std::size_t r = 0; r < rows.size(); r++) {
        auto& out = result.rows.emplace_back();
        const float height = heights[r];
        const float step = r + 1 == rows.size() ? height : std::max(0.0f, height + table.row_gap);
        out.bounds = {0.0f, y, 0.0f, y + step};
        for (auto& cell : rows[r]) {
            auto& layout = cell.layout;
            const float width =
                x[cell.column + cell.span] - x[cell.column] - gaps[cell.column + cell.span - 1];
            layout.bounds = {x[cell.column], y, x[cell.column] + width, y + height};
            layout.content = {layout.bounds.Min + ImVec2(cell.padding + cell.left, cell.padding),
                              layout.bounds.Max - ImVec2(cell.padding + cell.right, cell.padding)};
            layout.content.Max = ImMax(layout.content.Min, layout.content.Max);
            if (std::holds_alternative<ProgressCell>(*layout.cell)) {
                const auto start = ImVec2(layout.content.Min.x,
                                          layout.content.GetCenter().y - cell.minimum.y * 0.5f);
                layout.progress = {start, {layout.content.Max.x, start.y + cell.minimum.y}};
            }
            position_text(cell,
                          std::max(unit_widths[cell.column + cell.span - 1], cell.unit_width));
            if (layout.separator_text) {
                auto& run = *layout.separator_text;
                const auto bounds = text_bounds(run.text, run.font, run.font_size);
                run.pos += ImVec2(layout.bounds.Max.x +
                                      (gaps[cell.column + cell.span - 1] - bounds.GetWidth()) * 0.5f,
                                  y + (height - bounds.GetHeight()) * 0.5f);
                out.bounds.Max.x = layout.bounds.Max.x + gaps[cell.column + cell.span - 1];
            }
            if (layout.separator)
                layout.separator->Translate({layout.bounds.Max.x - 1.0f,
                                             y + (height - layout.separator->GetHeight()) * 0.5f});
            if (cell.has_content)
                out.bounds.Max.x = std::max(out.bounds.Max.x, x[cell.column] + width);
            out.cells.push_back(std::move(layout));
        }
        y = out.bounds.Max.y;
    }
    result.size.y = y;
    return result;
}
