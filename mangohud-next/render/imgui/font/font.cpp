#include <cstdint>
#include <cmath>
#include "file_utils.h"
#include "font_default.h"
#include "IconsForkAwesome.h"
#include "forkawesome.h"
#include "font_default.h"
#include "font.h"

static bool same_font_size(float a, float b) {
    return std::fabs(a - b) < 0.001f;
}

ImFont* Font::add_font(float font_size) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* font_atlas = io.Fonts;

    ImFont* font = font_atlas->AddFontFromMemoryCompressedBase85TTF(
        GetDefaultCompressedFontDataTTFBase85(), font_size);

    if (!font) {
        SPDLOG_ERROR("font creation failed size={}", font_size);
        return nullptr;
    }

    fonts.push_back({font_size, font});
    if (recreate_fonts_texture)
        recreate_fonts_texture();

    return font;
}

ImFont* Font::get(float font_size) {
    const float size = std::max(1.0f, font_size);

    for (auto& entry : fonts)
        if (entry.font && (same_font_size(entry.size, size) || same_font_size(entry.font->FontSize, size)))
            return entry.font;

    if (ImGui::GetIO().Fonts->Locked) {
        SPDLOG_ERROR("font size={} was not prepared before NewFrame", font_size);
        return text_font;
    }

    ImFont* font = add_font(size);
    if (!font)
        return text_font;

    return font;
}

void Font::create_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* font_atlas = io.Fonts;

    io.FontGlobalScale = scale;

    text_font = add_font(size);
    small_font = add_font(size / 2);

    SPDLOG_INFO(
        "created fonts atlas={} texid={} text_font={} text_font_atlas={}",
        fmt::ptr(font_atlas),
        (uint64_t)(uintptr_t)font_atlas->TexID,
        fmt::ptr(text_font),
        fmt::ptr(text_font ? text_font->ContainerAtlas : nullptr)
    );

   if (!text_font || !small_font) {
      SPDLOG_ERROR("font creation failed text_font={} small_font={}",
                  fmt::ptr(text_font), fmt::ptr(small_font));
   }
}
