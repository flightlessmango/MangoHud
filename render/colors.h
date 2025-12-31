#pragma once
#include "imgui.h"
#include <unordered_map>
#include <stdint.h>
#include <math.h>
#include <spdlog/spdlog.h>

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

enum overlay_transfer_function {
   NONE = 0,
   SRGB = (1 << 0),
   PQ = (1 << 1), /* HDR10 ST2084 */
   HLG = (1 << 2) /* HDR10 */
};

__attribute__((unused))
static uint32_t convert_colors_vk(VkFormat format, VkColorSpaceKHR colorspace)
{
   uint32_t transfer_function;
   switch (colorspace) {
      case VK_COLOR_SPACE_HDR10_ST2084_EXT:
         transfer_function = PQ;
         break;
      case VK_COLOR_SPACE_HDR10_HLG_EXT:
         transfer_function = HLG;
         break;
      case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
         transfer_function = SRGB;
         break;
      default:
         transfer_function = NONE;
         break;
   }

   if (transfer_function == NONE)
   {
      switch (format) {
         case VK_FORMAT_R8_SRGB:
         case VK_FORMAT_R8G8_SRGB:
         case VK_FORMAT_R8G8B8_SRGB:
         case VK_FORMAT_B8G8R8_SRGB:
         case VK_FORMAT_R8G8B8A8_SRGB:
         case VK_FORMAT_B8G8R8A8_SRGB:
         case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
         case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
         case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
         case VK_FORMAT_BC2_SRGB_BLOCK:
         case VK_FORMAT_BC3_SRGB_BLOCK:
         case VK_FORMAT_BC7_SRGB_BLOCK:
         case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
         case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
         case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
         case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
         case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
         case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
         case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
         case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
         case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
         case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
         case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
         case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
         case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
         case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
         case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
         case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
         case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
         case VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG:
         case VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG:
         case VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG:
         case VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG:
            transfer_function = SRGB;
            break;
         default: break;
      }
   }

   return transfer_function;
}
