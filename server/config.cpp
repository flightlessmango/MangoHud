#include <yaml-cpp/yaml.h>
#include <string_view>
#include <cstdint>

#include <spdlog/spdlog.h>
#include "config.h"
#include "common/table_structs.h"
#include "string_utils.h"

static bool validate_yaml(const YAML::Node& doc) {
    const auto hud = doc["hud_table"];
    if (!hud) {
        SPDLOG_ERROR("invalid config: hud_table missing");
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
    if (!cols) {
        SPDLOG_ERROR("invalid config: cols missing");
        return false;
    }
    if (!cols.IsScalar()) {
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

std::optional<TokenParts> splitToken(std::string_view s) {
  auto dot = s.find('.');
  if (dot == std::string_view::npos)
    return TokenParts{ .prefix = s, .suffix = {}, .index = 0, .has_index = false };


  std::string_view left = s.substr(0, dot);
  std::string_view right = s.substr(dot + 1);
  if (right.empty()) return std::nullopt;

  TokenParts out{};
  out.suffix = right;

  auto lb = left.find('[');
  if (lb == std::string_view::npos) {
    out.prefix = left;
    return out;
  }

  auto rb = left.find(']', lb + 1);
  if (rb == std::string_view::npos) return std::nullopt;

  out.prefix = left.substr(0, lb);
  std::string_view idx_str = left.substr(lb + 1, rb - (lb + 1));
  if (idx_str.empty()) return std::nullopt;

  int idx = 0;
  auto res = std::from_chars(idx_str.data(), idx_str.data() + idx_str.size(), idx);
  if (res.ec != std::errc{} || res.ptr != idx_str.data() + idx_str.size()) return std::nullopt;

  out.index = idx;
  out.has_index = true;
  return out;
}

// std::optional<MetricRef> metricRefFromString(std::string_view token) {
//     auto parts = splitToken(token);
//     if (!parts) return std::nullopt;
//     const TokenParts& p = *parts;

//     if (p.suffix.empty()) {
//         if (p.prefix == "FPS")       return MetricRef{MetricId::FPS};
//         if (p.prefix == "FRAMETIME") return MetricRef{MetricId::FRAMETIME};
//         return std::nullopt;
//     }

//     if (p.prefix == "GPU") {
//         if (p.suffix == "LOAD")  return MetricRef{MetricId::GPU_LOAD,  p.index};
//         if (p.suffix == "TEMP")  return MetricRef{MetricId::GPU_TEMP,  p.index};
//         if (p.suffix == "CLOCK") return MetricRef{MetricId::GPU_CLOCK, p.index};
//         if (p.suffix == "POWER") return MetricRef{MetricId::GPU_POWER, p.index};
//     } else if (p.prefix == "VRAM") {
//         if (p.suffix == "USED")  return MetricRef{MetricId::VRAM_USED,  p.index};
//         if (p.suffix == "CLOCK") return MetricRef{MetricId::VRAM_CLOCK, p.index};
//     } else if (p.prefix == "CPU") {
//         if (p.suffix == "LOAD")  return MetricRef{MetricId::CPU_LOAD};
//         if (p.suffix == "TEMP")  return MetricRef{MetricId::CPU_TEMP};
//         if (p.suffix == "CLOCK") return MetricRef{MetricId::CPU_CLOCK};
//     } else if (p.prefix == "RAM") {
//         if (p.suffix == "USED")  return MetricRef{MetricId::RAM_USED};
//     }

//     return std::nullopt;
// }


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

static bool parse_yaml(HudTable& table, YAML::Node doc) {
    try {
        table.cols = doc["hud_table"]["cols"].as<int>();
        table.font_size = doc["hud_table"]["font_size"].as<int>();
    } catch (YAML::BadConversion& e) {
        SPDLOG_ERROR("cols must be an integer in {}", e.what());
        return false;
    }
    if (table.cols <= 0) {
        SPDLOG_ERROR("config cols has to be >= 0 {}", table.cols);
        return false;
    }

    YAML::Node rows = doc["hud_table"]["rows"];

    table.rows.clear();

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

        if (int(parsed_row.size()) > table.cols)
            SPDLOG_ERROR("Column count has been exceeded {} > {}", parsed_row.size(), table.cols);

        table.rows.push_back(parsed_row);
    }
    return true;
}

std::unique_ptr<HudTable> parse_hud_table(const char* file) {
    YAML::Node doc;
    auto out = std::make_unique<HudTable>();
    try {
        doc = YAML::LoadFile(file);
    } catch (const YAML::BadFile& e) {
        SPDLOG_ERROR("{}", e.what());
        return nullptr;
    } catch (const YAML::ParserException& e) {
        SPDLOG_ERROR("{} in {}", e.what(), file);
        return nullptr;
    }

    if (!validate_yaml(doc)) return nullptr;
    try {
        if (!parse_yaml(*out, doc)) return nullptr;
    } catch (const YAML::Exception& e) {
        SPDLOG_ERROR("YAML error while parsing HUD table: {}", e.what());
        return nullptr;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error while parsing HUD table: {}", e.what());
        return nullptr;
    } catch (...) {
        SPDLOG_ERROR("Unknown error while parsing HUD table");
        return nullptr;
    }
    return out;
}
