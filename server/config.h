#pragma once
#include <string>
#include <optional>
#include <variant>
#include <vector>
#include <unordered_set>
#include <inttypes.h>
#include <charconv>
#include <memory>
#include <yaml-cpp/yaml.h>
#include <unordered_map>
#include <string>
#include <string_view>
#include <variant>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include "common/table_structs.h"

#ifdef Bool
#undef Bool
#endif

struct configSig {
    bool exists = false;
    std::int64_t size = 0;
    std::int64_t sec  = 0;
    std::int64_t nsec = 0;
};

inline std::string get_config_dir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (*xdg) return std::string(xdg);
    }
    if (const char* home = std::getenv("HOME")) {
        if (*home) return std::string(home) + "/.config";
    }
    throw std::runtime_error("Cannot determine config directory (no XDG_CONFIG_HOME or HOME)");
}

class Config {
public:
    std::mutex m;
    std::shared_ptr<hudTable> table;

    Config() {
        config_path = get_config_dir() + "/MangoHud/MangoHud.yml";
        table = std::make_shared<hudTable>();
        for (const auto& [key, spec] : possible_)
            options_.emplace(std::string(key), spec.val);
    }

    bool inited = false;

    void update(YAML::Node doc) {
        std::lock_guard lock(m);
        for (const auto& [key, spec] : possible_)
            options_.emplace(std::string(key), spec.val);

        load_yaml(doc);
        inited = true;
    }

    template <class T>
    const T& get(std::string_view key) const
    {
        const Value& v = raw(key);
        if (const T* p = std::get_if<T>(&v))
        {
            return *p;
        }

        SPDLOG_ERROR("type mismatch for key: {}", key);
        std::terminate();
    }

    void load_yaml(const YAML::Node& root)
    {
        if (!root || !root.IsMap()) {
            SPDLOG_DEBUG("config root must be a map");
            inited = true;
            return;
        }

        for (const auto& [key_sv, spec] : possible_)
        {
            YAML::Node n = root[std::string(key_sv)];
            if (!n)
                continue;

            try {
                options_[std::string(key_sv)] = to_value(spec.type, n);
            } catch (const YAML::BadConversion&) {
                SPDLOG_ERROR("bad conversion for key: {}", key_sv);
            }
        }
    }

    bool maybe_reload_config() {
        if (config_path.empty())
            return false;

        configSig now;
        read_sig(config_path.c_str(), now);
        if (sig_changed(prev_sig, now)) {
            prev_sig = now;
            parse_table(config_path);
            return true;
        }

        return false;
    }

    void parse_table(std::string);
    bool parse_table_yaml(hudTable& table, YAML::Node doc);

private:
    enum Type { Int, Double, Bool, String };
    using Value = std::variant<int, double, bool, std::string>;
    using ValueMap = std::unordered_map<std::string, Value>;
    std::string config_path;
    ValueMap options_;
    configSig prev_sig {};

    struct Spec {
        Type type;
        Value val;
    };

    static inline const std::unordered_map<std::string_view, Spec> possible_ = {
        {"font_size", Spec{Int, 24}},
        {"fps_limit", Spec{Double, 0.0}},
    };

    const Value& raw(std::string_view key) const
    {
        auto it = options_.find(std::string(key));
        if (it == options_.end()) {
            SPDLOG_ERROR("missing key: {}", key);
            std::terminate();
        }

        return it->second;
    }

    static Value to_value(Type type, const YAML::Node& n)
    {
        switch (type)
        {
        case Int:
            return n.as<int>();
        case Double:
            return n.as<double>();
        case Bool:
            return n.as<bool>();
        case String:
            return n.as<std::string>();
        }
        throw std::runtime_error("unhandled Spec");
    }

    bool sig_changed(const configSig& a, const configSig& b) {
        return a.exists != b.exists ||
            a.size   != b.size   ||
            a.sec    != b.sec    ||
            a.nsec   != b.nsec;
    }

    bool read_sig(const char* path, configSig& out) {
        struct stat st {};
        if (::stat(path, &st) == 0) {
            out.exists = true;
            out.size = static_cast<std::int64_t>(st.st_size);
            out.sec  = static_cast<std::int64_t>(st.st_mtim.tv_sec);
            out.nsec = static_cast<std::int64_t>(st.st_mtim.tv_nsec);
            return true;
        }

        if (errno == ENOENT || errno == ENOTDIR) {
            out = configSig{};
            return true;
        }

        out = configSig{};
        return false;
    }
};

inline constexpr std::string_view HudYaml = R"YAML(
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
