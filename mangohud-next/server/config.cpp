#include <yaml-cpp/yaml.h>
#include <string_view>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <filesystem>

#include "config.h"
#include "common/table_structs.h"
#include "string_utils.h"

static constexpr const char* default_cell_color = "FFFFFFFF";

static bool validate_table(const YAML::Node& table) {
    if (!table) {
        SPDLOG_DEBUG("table missing");
        return false;
    }

    const auto rows = table["rows"];
    if (!rows) {
        SPDLOG_ERROR("invalid config: rows missing");
        return false;
    }
    if (!rows.IsSequence()) {
        SPDLOG_ERROR("rows is not a list");
        return false;
    }

    const auto cols = table["cols"];
    if (cols && !cols.IsScalar()) {
        SPDLOG_ERROR("cols must be a scalar integer");
        return false;
    }

    for (const auto& row : rows) {
        if (!row.IsSequence()) {
            SPDLOG_ERROR("row is not a list");
            return false;
        }
        for (const auto& cell : row) {
            if (cell.IsNull()) continue;
            if (!cell.IsMap()) {
                SPDLOG_ERROR("cell must be null or a map/object");
                return false;
            }
            if (cell["table"] && !validate_table(cell["table"]))
                return false;
        }
    }

    return true;
}

static bool validate_yaml(const YAML::Node& doc) {
    const auto hud = doc["hud"];
    if (hud) {
        const auto windows = hud["windows"];
        if (!windows) {
            SPDLOG_ERROR("invalid config: hud.windows missing");
            return false;
        }
        if (!windows.IsSequence()) {
            SPDLOG_ERROR("hud.windows is not a list");
            return false;
        }
        if (windows.size() == 0) {
            SPDLOG_ERROR("hud.windows must contain at least one window");
            return false;
        }

        for (const auto& window : windows) {
            if (!window.IsMap()) {
                SPDLOG_ERROR("hud window is not a map/object");
                return false;
            }
            const auto background = window["background"];
            if (background && !background.IsScalar()) {
                SPDLOG_ERROR("hud window background must be a boolean");
                return false;
            }
            if (background) {
                try {
                    background.as<bool>();
                } catch (const YAML::BadConversion&) {
                    SPDLOG_ERROR("hud window background must be a boolean");
                    return false;
                }
            }
            const auto padding = window["padding"];
            if (padding && !padding.IsScalar()) {
                SPDLOG_ERROR("hud window padding must be a number");
                return false;
            }
            if (padding) {
                try {
                    if (padding.as<float>() < 0.0f) {
                        SPDLOG_ERROR("hud window padding cannot be negative");
                        return false;
                    }
                } catch (const YAML::BadConversion&) {
                    SPDLOG_ERROR("hud window padding must be a number");
                    return false;
                }
            }
            const auto position = window["position"];
            if (position) {
                if (!position.IsSequence() || position.size() != 2) {
                    SPDLOG_ERROR("hud window position must be a two-item list");
                    return false;
                }
                try {
                    position[0].as<float>();
                    position[1].as<float>();
                } catch (const YAML::BadConversion&) {
                    SPDLOG_ERROR("hud window position must contain numbers");
                    return false;
                }
            }
            if (!validate_table(window["table"]))
                return false;
        }

        return true;
    }

    return validate_table(doc["hud_table"]);
}

static MetricRef parse_value(const YAML::Node& v) {
    if (!v) throw std::runtime_error("missing value");

    if (v.IsScalar())
        return { "GLOBAL", v.as<std::string>() };

    if (v.IsSequence()) {
        if (v.size() == 2)
            return { v[0].as<std::string>(), v[1].as<std::string>() };

        if (v.size() == 3)
            return { v[0].as<std::string>() + std::to_string(v[1].as<int>()),
                     v[2].as<std::string>() };
    }

    throw std::runtime_error("value must be scalar or sequence");
}

static CellStyle parse_cell_style(const YAML::Node& cell) {
    CellStyle style;

    if (cell["font_size"])
        style.font_size = cell["font_size"].as<float>();

    if (cell["font_scale"])
        style.font_scale = cell["font_scale"].as<float>();

    if (cell["colspan"])
        style.colspan = std::max(1, cell["colspan"].as<int>());

    return style;
}

static void append_colspan_placeholders(std::vector<MaybeCell>& row, const CellStyle& style) {
    for (int i = 1; i < style.colspan; i++)
        row.push_back(std::nullopt);
}

static bool parse_table_node(hudTable& table, YAML::Node table_node, int font_size) {
    YAML::Node rows = table_node["rows"];
    table.rows.clear();
    table.font_size = font_size;
    std::size_t cols = 0;
    for (auto row : rows) {
        std::vector<MaybeCell> parsed_row;

        for (auto cell : row) {
            if (cell.IsNull()) {
                parsed_row.push_back(std::nullopt);
                continue;
            }

            if (cell["text"]) {
                TextCell tc;
                tc.text = cell["text"].as<std::string>();
                tc.color = cell["color"] ? cell["color"].as<std::string>() : default_cell_color;
                tc.style = parse_cell_style(cell);

                parsed_row.push_back(Cell{tc});
                append_colspan_placeholders(parsed_row, tc.style);
                continue;
            };

            if (cell["value"]) {
                ValueCell vc;
                vc.ref = parse_value(cell["value"]);
                vc.unit = cell["unit"] ? cell["unit"].as<std::string>() : std::string();
                vc.color = cell["color"] ? cell["color"].as<std::string>() : default_cell_color;
                if (cell["precision"])
                    try_stoi(vc.precision, cell["precision"].as<std::string>());
                vc.style = parse_cell_style(cell);

                parsed_row.push_back(Cell{vc});
                append_colspan_placeholders(parsed_row, vc.style);
                continue;
            }

            if (cell["graph"]) {
                GraphCell gc;
                gc.ref = parse_value(cell["graph"]);
                gc.style = parse_cell_style(cell);
                parsed_row.push_back(Cell{gc});
                append_colspan_placeholders(parsed_row, gc.style);
                continue;
            }

            if (cell["exec"]) {
                ExecCell ec;
                ec.command = cell["exec"].as<std::string>();
                ec.unit = cell["unit"] ? cell["unit"].as<std::string>() : std::string();
                ec.color = cell["color"] ? cell["color"].as<std::string>() : default_cell_color;
                ec.style = parse_cell_style(cell);

                parsed_row.push_back(Cell{ec});
                append_colspan_placeholders(parsed_row, ec.style);
                continue;
            }

            if (cell["table"]) {
                TableCell tc;
                tc.table = std::make_shared<hudTable>();
                tc.style = parse_cell_style(cell);
                parse_table_node(*tc.table, cell["table"], font_size);
                parsed_row.push_back(Cell{tc});
                append_colspan_placeholders(parsed_row, tc.style);
                continue;
            }
        }

        cols = std::max(cols, parsed_row.size());
        table.rows.push_back(parsed_row);
    }

    table.cols = cols;
    return true;
}

bool Config::parse_table_yaml(HudConfig& hud, YAML::Node doc) {
    std::lock_guard lock(m);
    hud.windows.clear();

    const int font_size = get<int>("font_size");
    const auto hud_node = doc["hud"];
    if (hud_node) {
        for (const auto& window_node : hud_node["windows"]) {
            HudWindow window;
            if (window_node["background"])
                window.background = window_node["background"].as<bool>();
            if (window_node["padding"])
                window.padding = window_node["padding"].as<float>();
            if (window_node["position"])
                window.position = {window_node["position"][0].as<float>(), window_node["position"][1].as<float>()};
            parse_table_node(window.table, window_node["table"], font_size);
            hud.windows.push_back(std::move(window));
        }
        if (hud.windows.empty())
            hud.windows.emplace_back();

        return true;
    }

    hud.windows.emplace_back();
    return parse_table_node(hud.default_window().table, doc["hud_table"], font_size);
}

void Config::parse_table(std::string file) {
    auto load_default = [&]() -> void {
        try {
            YAML::Node def = YAML::Load(std::string{HudYaml});
            parse_table_yaml(*hud, def);
        } catch (const YAML::Exception& e) {
            SPDLOG_ERROR("YAML error while loading default HUD config: {}", e.what());
            return;
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Error while loading default HUD config: {}", e.what());
            return;
        } catch (...) {
            SPDLOG_ERROR("Unknown error while loading default HUD config");
            return;
        }
    };

    try {
        if (!std::filesystem::exists(file)) {
            SPDLOG_DEBUG("Config file doesn't exist, falling back to default");
            update(YAML::Node());
            load_default();
            return;
        }

        YAML::Node doc = YAML::LoadFile(file);
        update(doc["options"]);

        if (!validate_yaml(doc)) {
            SPDLOG_ERROR("HUD config validation failed: {}", file);
            SPDLOG_DEBUG("Falling back to default config");
            load_default();
            return;
        }

        parse_table_yaml(*hud, doc);
        return;

    } catch (const YAML::BadFile& e) {
        SPDLOG_ERROR("{} {}", e.what(), file);
    } catch (const YAML::ParserException& e) {
        SPDLOG_ERROR("{} in {}", e.what(), file);
    } catch (const YAML::Exception& e) {
        SPDLOG_ERROR("YAML error while parsing HUD table from {}: {}", file, e.what());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error while parsing HUD table from {}: {}", file, e.what());
    } catch (...) {
        SPDLOG_ERROR("Unknown error while parsing HUD table from {}", file);
    }

    SPDLOG_DEBUG("Falling back to default config");
    load_default();
}
