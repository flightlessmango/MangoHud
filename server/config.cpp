#include <yaml-cpp/yaml.h>
#include <string_view>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <filesystem>

#include "config.h"
#include "common/table_structs.h"
#include "string_utils.h"

static constexpr std::string_view HudYaml = R"YAML(
hud_table:
  rows:
    - [ {text: GPU, color: "2e9762"},
        {value: [GPU, 0, LOAD]},
        {value: [GPU, 0, TEMP]}
    ]

    - [ null,
        {value: [GPU, 0, CORE_CLOCK]},
        {value: [GPU, 0, POWER]}
    ]

    - [ {text: CPU, color: "2e97cb"},
        {value: [CPU, LOAD]},
        {value: [CPU, TEMP]}
    ]

    - [ null,
        {value: [CPU, FREQ]},
        {value: [CPU, POWER]}
    ]

    - [ {text: VRAM, color: "ad64c1"},
        {value: [GPU, 0, VRAM_USED]},
        {value: [GPU, 0, VRAM_CLOCK]}
    ]

    - [ {text: RAM, color: "c26693"},
        {value: [RAM, USED]},
        {value: [RAM, TEMP]}
    ]

    - [ {text: VULKAN, color: "eb5b5b"},
        {value: FPS},
        {value: FRAMETIME}
    ]

    - [ {graph: FRAMETIMES}]
)YAML";

static bool validate_yaml(const YAML::Node& doc) {
    const auto hud = doc["hud_table"];
    if (!hud) {
        SPDLOG_DEBUG("hud_table missing");
        return false;
    }

    const auto rows = hud["rows"];
    if (!rows) {
        SPDLOG_ERROR("invalid config: rows missing");
        return false;
    }
    if (!rows.IsSequence()) {
        SPDLOG_ERROR("rows is not a list");
        return false;
    }

    const auto cols = hud["cols"];
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
        }
    }

    return true;
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

static bool parse_yaml(hudTable& table, YAML::Node doc) {
    YAML::Node rows = doc["hud_table"]["rows"];
    table.rows.clear();
    std::size_t cols = 0;
    for (auto row : rows) {
        cols = std::max(cols, row.size());
        std::vector<MaybeCell> parsed_row;

        for (auto cell : row) {
            if (cell.IsNull()) {
                parsed_row.push_back(std::nullopt);
                continue;
            }

            if (cell["text"]) {
                TextCell tc;
                tc.text = cell["text"].as<std::string>();
                tc.color = cell["color"] ? cell["color"].as<std::string>() : "FFFFFFFF";

                parsed_row.push_back(Cell{tc});
                continue;
            };

            if (cell["value"]) {
                ValueCell vc;
                vc.ref = parse_value(cell["value"]);
                vc.unit = cell["unit"] ? cell["unit"].as<std::string>() : std::string();
                vc.color = cell["color"] ? cell["color"].as<std::string>() : "FFFFFFFF";
                if (cell["precision"])
                    try_stoi(vc.precision, cell["precision"].as<std::string>());

                parsed_row.push_back(Cell{vc});
                continue;
            }

            if (cell["graph"]) {
                GraphCell gc;
                gc.ref = parse_value(cell["graph"]);
                parsed_row.push_back(Cell{gc});
                continue;
            }
        }

        table.rows.push_back(parsed_row);
    }

    table.cols = cols;
    return true;
}

std::unique_ptr<hudTable> parse_hud_table(const char* file) {
    auto out = std::make_unique<hudTable>();

    auto load_default = [&]() -> bool {
        try {
            YAML::Node def = YAML::Load(std::string{HudYaml});
            return parse_yaml(*out, def);
        } catch (const YAML::Exception& e) {
            SPDLOG_ERROR("YAML error while loading default HUD config: {}", e.what());
            return false;
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Error while loading default HUD config: {}", e.what());
            return false;
        } catch (...) {
            SPDLOG_ERROR("Unknown error while loading default HUD config");
            return false;
        }
    };

    try {
        if (!cfg)
            cfg = std::make_shared<Config>();

        if (!std::filesystem::exists(file)) {
            SPDLOG_DEBUG("Config file doesn't exist, falling back to default");
            if (!load_default()) return nullptr;
            cfg->update(YAML::Node());
            return out;
        }

        YAML::Node doc = YAML::LoadFile(file);
        cfg->update(doc["options"]);

        if (!validate_yaml(doc)) {
            SPDLOG_ERROR("HUD config validation failed: {}", file);
            SPDLOG_DEBUG("Falling back to default config");
            if (!load_default()) return nullptr;
            return out;
        }

        if (!parse_yaml(*out, doc)) {
            SPDLOG_ERROR("HUD config parse returned failure: {}", file);
            SPDLOG_DEBUG("Falling back to default config");
            if (!load_default()) return nullptr;
            return out;
        }

        return out;
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
    if (!load_default()) return nullptr;
    return out;
}
