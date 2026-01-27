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

std::unique_ptr<hudTable> parse_hud_table(const char* file);
inline std::mutex config_m;

class Config {
public:
    std::atomic<bool> config_changed {false};
    Config() {
        std::lock_guard lock(config_m);
        for (const auto& [key, spec] : possible_)
            options_.emplace(std::string(key), spec.val);
    };

    bool inited = false;

    void update(YAML::Node doc) {
        std::lock_guard lock(config_m);
        for (const auto& [key, spec] : possible_)
            options_.emplace(std::string(key), spec.val);

        load_yaml(doc);
        config_changed.store(true);
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
        throw std::runtime_error("type mismatch for key: " + std::string(key));
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

private:
    enum Type { Int, Double, Bool, String };
    using Value = std::variant<int, double, bool, std::string>;
    using ValueMap = std::unordered_map<std::string, Value>;
    ValueMap options_;

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
        if (it == options_.end())
            SPDLOG_DEBUG("missing key: {}", key);

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
};

inline std::shared_ptr<Config> cfg;

__attribute__((unused))
static std::shared_ptr<Config> get_cfg() {
    std::lock_guard<std::mutex> lock(config_m);
    auto local = cfg;

    if (local && local->inited) {
        return local;
    }

    SPDLOG_DEBUG("Tried to access config before initialization (cfg={}, inited={})",
                    (void*)local.get(), local ? local->inited : false);

    return nullptr;
}

