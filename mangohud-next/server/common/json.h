#pragma once

#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

template <typename T>
inline void append_json_string(std::string& out, const T& value) {
    static constexpr char hex[] = "0123456789abcdef";
    const std::string_view text(value);

    out.push_back('"');
    for (unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 0xf]);
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
}

template <typename T>
inline void append_json_value(std::string& out, const T& value) {
    using V = std::decay_t<T>;

    if constexpr (std::is_same_v<V, std::vector<float>>) {
        out.push_back('[');
        for (size_t i = 0; i < value.size(); i++) {
            if (i)
                out.push_back(',');
            append_json_value(out, value[i]);
        }
        out.push_back(']');
    } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        append_json_string(out, value);
    } else if constexpr (std::is_same_v<V, bool>) {
        out += value ? "true" : "false";
    } else if constexpr (std::is_integral_v<V>) {
        out += std::to_string(value);
    } else if constexpr (std::is_floating_point_v<V>) {
        if (!std::isfinite(value)) {
            out += "null";
            return;
        }

        char buffer[64];
        auto [ptr, ec] = std::to_chars(std::begin(buffer), std::end(buffer), value);
        if (ec == std::errc())
            out.append(buffer, ptr);
        else
            out += "null";
    }
}
