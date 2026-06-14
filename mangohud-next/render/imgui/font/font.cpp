#include <cstdint>
#include "file_utils.h"
#include "font_default.h"
#include "IconsForkAwesome.h"
#include "forkawesome.h"
#include "font_default.h"
#include "font.h"

std::mutex m;

void Font::create_fonts() {
    std::lock_guard lock(m);

    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* font_atlas = io.Fonts;

    io.FontGlobalScale = scale;

    text_font = font_atlas->AddFontFromMemoryCompressedBase85TTF(
        GetDefaultCompressedFontDataTTFBase85(), size);

    small_font = font_atlas->AddFontFromMemoryCompressedBase85TTF(
        GetDefaultCompressedFontDataTTFBase85(), size / 2);

    SPDLOG_INFO(
        "created fonts atlas={} texid={} text_font={} text_font_atlas={}",
        fmt::ptr(font_atlas),
        (uint64_t)(uintptr_t)font_atlas->TexID,
        fmt::ptr(text_font),
        fmt::ptr(text_font->ContainerAtlas)
    );

   if (!text_font || !small_font) {
      SPDLOG_ERROR("font creation failed text_font={} small_font={}",
                  fmt::ptr(text_font), fmt::ptr(small_font));
   }
}
