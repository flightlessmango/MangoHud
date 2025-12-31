#include <cstdint>
#include "file_utils.h"
#include "font_default.h"
#include "IconsForkAwesome.h"
#include "forkawesome.h"
#include "font_default.h"
#include "font.h"

std::unique_ptr<Font> fonts;

void Font::create_fonts(ImFontAtlas* font_atlas)
{

   ImGuiIO& io = ImGui::GetIO();
   io.FontGlobalScale = scale;
   text_font = io.Fonts->AddFontFromMemoryCompressedBase85TTF(
               GetDefaultCompressedFontDataTTFBase85(), size);
   small_font = io.Fonts->AddFontFromMemoryCompressedBase85TTF(
               GetDefaultCompressedFontDataTTFBase85(), size / 2);
}
