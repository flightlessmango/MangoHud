#pragma once
#include "imgui.h"
#include <unordered_map>
#include <stdint.h>
#include <math.h>
#include <spdlog/spdlog.h>

static float SRGBToLinear(float in)
{
    if (in <= 0.04045f)
        return in / 12.92f;
    else
        return powf((in + 0.055f) / 1.055f, 2.4f);
}

static ImVec4 SRGBToLinear(ImVec4 col)
{
    col.x = SRGBToLinear(col.x);
    col.y = SRGBToLinear(col.y);
    col.z = SRGBToLinear(col.z);
    // Alpha component is already linear
    return col;
}

static inline ImVec4 PackedRGBA8ToImVec4(uint32_t rgba) {
    float r = ((rgba >> 24) & 0xff) / 255.0f;
    float g = ((rgba >> 16) & 0xff) / 255.0f;
    float b = ((rgba >>  8) & 0xff) / 255.0f;
    float a = ((rgba >>  0) & 0xff) / 255.0f;
    ImVec4 col = {r, g, b, a};
    return col;
    // return SRGBToLinear(col);
}

static bool parse_hex_u32(std::string_view s, uint32_t& out) {
    if (s.empty()) return false;

    uint32_t v = 0;
    for (char c : s) {
        uint32_t d = 0;
        if (c >= '0' && c <= '9') d = uint32_t(c - '0');
        else if (c >= 'a' && c <= 'f') d = uint32_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = uint32_t(c - 'A' + 10);
        else return false;

        v = (v << 4) | d;
    }
    out = v;
    return true;
}

static uint32_t parse_color(std::string_view input) {
    std::string_view s = input;
    if (!s.empty() && s.front() == '#') s.remove_prefix(1);

    if (s.size() == 6) {
        uint32_t rgb = 0;
        if (!parse_hex_u32(s, rgb)) return 0x000000FFu;
        return (rgb << 8) | 0xFFu; // RRGGBBFF
    }

    if (s.size() == 8) {
        uint32_t rgba = 0;
        if (!parse_hex_u32(s, rgba)) return 0x000000FFu;
        return rgba; // RRGGBBAA
    }

    SPDLOG_ERROR("invalid color '{}': expected RRGGBB or RRGGBBAA", input);
    return 0x000000FFu;
}


class ColorCache {
public:
    const ImVec4& get(std::string s) {
        std::unique_lock lock(m);
        uint32_t packed_rgba = parse_color(s);
        auto [it, inserted] = cache.try_emplace(packed_rgba, ImVec4{});
        if (inserted)
            it->second = PackedRGBA8ToImVec4(packed_rgba);

        return it->second;
    }

    void clear() { cache.clear(); }

private:
    std::unordered_map<uint32_t, ImVec4> cache;
    std::mutex m;
};
