#include "overlay.h"
#include "file_utils.h"
#include "font_default.h"

void create_fonts(const overlay_params& params, ImFont*& small_font, ImFont*& text_font)
{
   auto& io = ImGui::GetIO();
   ImGui::GetIO().FontGlobalScale = params.font_scale; // set here too so ImGui::CalcTextSize is correct
   float font_size = params.font_size;
   if (font_size < FLT_EPSILON)
      font_size = 24;

   float font_size_text = params.font_size_text;
   if (font_size_text < FLT_EPSILON)
      font_size_text = font_size;
   static const ImWchar default_range[] =
   {
      0x0020, 0x00FF, // Basic Latin + Latin Supplement
      //0x0100, 0x017F, // Latin Extended-A
      //0x2103, 0x2103, // Degree Celsius
      //0x2109, 0x2109, // Degree Fahrenheit
      0,
   };

   ImVector<ImWchar> glyph_ranges;
   ImFontGlyphRangesBuilder builder;
   builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
   if (params.font_glyph_ranges & FG_KOREAN)
      builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
   if (params.font_glyph_ranges & FG_CHINESE_FULL)
      builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
   if (params.font_glyph_ranges & FG_CHINESE_SIMPLIFIED)
      builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
   if (params.font_glyph_ranges & FG_JAPANESE)
      builder.AddRanges(io.Fonts->GetGlyphRangesJapanese()); // Not exactly Shift JIS compatible?
   if (params.font_glyph_ranges & FG_CYRILLIC)
      builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
   if (params.font_glyph_ranges & FG_THAI)
      builder.AddRanges(io.Fonts->GetGlyphRangesThai());
   if (params.font_glyph_ranges & FG_VIETNAMESE)
      builder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());
   if (params.font_glyph_ranges & FG_LATIN_EXT_A) {
      static const ImWchar latin_ext_a[] { 0x0100, 0x017F, 0 };
      builder.AddRanges(latin_ext_a);
   }
   if (params.font_glyph_ranges & FG_LATIN_EXT_B) {
      static const ImWchar latin_ext_b[] { 0x0180, 0x024F, 0 };
      builder.AddRanges(latin_ext_b);
   }
   builder.BuildRanges(&glyph_ranges);

   // If both font_file and text_font_file are the same then just use "default" font
   bool same_font = (params.font_file == params.font_file_text || params.font_file_text.empty());
   bool same_size = (font_size == font_size_text);

   // ImGui takes ownership of the data, no need to free it
   if (!params.font_file.empty() && file_exists(params.font_file)) {
      io.Fonts->AddFontFromFileTTF(params.font_file.c_str(), font_size, nullptr, same_font && same_size ? glyph_ranges.Data : default_range);
      if (params.no_small_font)
         small_font = io.Fonts->Fonts[0];
      else
         small_font = io.Fonts->AddFontFromFileTTF(params.font_file.c_str(), font_size * 0.55f, nullptr, default_range);
   } else {
      const char* ttf_compressed_base85 = GetDefaultCompressedFontDataTTFBase85();
      io.Fonts->AddFontFromMemoryCompressedBase85TTF(ttf_compressed_base85, font_size, nullptr, default_range);
      if (params.no_small_font)
         small_font = io.Fonts->Fonts[0];
      else
         small_font = io.Fonts->AddFontFromMemoryCompressedBase85TTF(ttf_compressed_base85, font_size * 0.55f, nullptr, default_range);
   }

   auto font_file_text = params.font_file_text;
   if (font_file_text.empty())
      font_file_text = params.font_file;

   if ((!same_font || !same_size) && file_exists(font_file_text))
      text_font = io.Fonts->AddFontFromFileTTF(font_file_text.c_str(), font_size_text, nullptr, glyph_ranges.Data);
   else
      text_font = io.Fonts->Fonts[0];

   io.Fonts->Build();
}