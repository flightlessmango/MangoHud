#include <spdlog/spdlog.h>
#include <artery-font/std-artery-font.h>
#include <artery-font/stdio-serialization.h>
#include <artery-font/structures.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "overlay.h"
#include "file_utils.h"
#include "string_utils.h"
#include "font_default.h"
#include "IconsForkAwesome.h"
#include "forkawesome.h"

// Generate font in binary format arfont with https://github.com/Chlumsky/msdf-atlas-gen
// ./bin/msdf-atlas-gen -type mtsdf -font /usr/share/fonts/TTF/FiraSans-Regular.ttf -format bin -size 16 -pots -charset charset_ascii.txt -arfont ascii.arfont
void create_font_from_mtsdf(const overlay_params& params, ImFont*& small_font, ImFont*& text_font)
{
   auto& io = ImGui::GetIO();
   io.Fonts->Clear();
   ImGui::GetIO().FontGlobalScale = params.font_scale; // set here too so ImGui::CalcTextSize is correct

    artery_font::StdArteryFont<float> arfont {};
    artery_font::readFile(arfont, params.font_file.c_str());
    SPDLOG_DEBUG("arfont: size: {}x{} {} {}",
                arfont.images[0].width, arfont.images[0].height,
                arfont.images[0].imageType,
                arfont.images[0].encoding);

    if (arfont.images.length() < 1)
        return;

    if (arfont.images[0].imageType != artery_font::IMAGE_MTSDF)
    {
        SPDLOG_ERROR("Font type is not MTSDF");
        return;
    }

    if (arfont.images[0].encoding != artery_font::ImageEncoding::IMAGE_RAW_BINARY)
    {
        SPDLOG_ERROR("Font image encoding is not raw binary");
        return;
    }

    const auto& v = arfont.variants[0];
    const auto& img = arfont.images[0];

    io.Fonts->ClearInputData();
    ImFontAtlasBuildInit(io.Fonts);
    // Clear atlas
    io.Fonts->TexID = (ImTextureID)NULL;
    io.Fonts->TexWidth = img.width;
    io.Fonts->TexHeight = img.height;
    io.Fonts->TexUvWhitePixel = ImVec2(0.0f, 0.0f);

    io.Fonts->TexHeight += IM_DRAWLIST_TEX_LINES_WIDTH_MAX + 1 + 32; // give space for custom stuff
    io.Fonts->TexHeight = (io.Fonts->Flags & ImFontAtlasFlags_NoPowerOfTwoHeight) ? (io.Fonts->TexHeight + 1) : ImUpperPowerOfTwo(io.Fonts->TexHeight);
    if (io.Fonts->TexWidth < 256)
        io.Fonts->TexWidth = 256; // give space for custom stuff
    io.Fonts->TexUvScale = ImVec2(1.0f / io.Fonts->TexWidth, 1.0f / io.Fonts->TexHeight);

    ImFontConfig font_cfg;
    io.Fonts->Fonts.push_back(IM_NEW(ImFont));
    ImFont* font = io.Fonts->Fonts.back();
    font->Ascent = v.metrics.ascender * v.metrics.fontSize;
    font->Descent = v.metrics.descender * v.metrics.fontSize;
    font->FontSize = v.metrics.fontSize; // * v.metrics.lineHeight;
    font->Scale = 1.0f;
    font->ContainerAtlas = io.Fonts;
    font_cfg.DstFont = font;
    strncpy(font_cfg.Name, (const char *)v.name, min(40, v.name.length()));
    io.Fonts->ConfigData.push_back(font_cfg);
    font->ConfigData = &io.Fonts->ConfigData.back();
    font->ConfigDataCount = 1;
    io.Fonts->ClearTexData(); // invalidate texture data

    for (const auto& g : v.glyphs.vector)
    {
        float u0 = g.imageBounds.l * io.Fonts->TexUvScale.x;
        float v0 = (img.height - g.imageBounds.t) * io.Fonts->TexUvScale.y;
        float u1 = g.imageBounds.r * io.Fonts->TexUvScale.x;
        float v1 = (img.height - g.imageBounds.b) * io.Fonts->TexUvScale.y;
        font->AddGlyph(nullptr, (ImWchar)g.codepoint,
                       g.planeBounds.l * v.metrics.fontSize,
                       (1.0f - g.planeBounds.t) * v.metrics.fontSize,
                       g.planeBounds.r * v.metrics.fontSize,
                       (1.0f - g.planeBounds.b) * v.metrics.fontSize,
                       u0, v0, u1, v1,
                       g.advance.h * v.metrics.fontSize);
    }

    // Allocate texture
    io.Fonts->TexPixelsRGBA32 = (unsigned int*)IM_ALLOC(io.Fonts->TexWidth * io.Fonts->TexHeight * 4);
    unsigned char* src = (unsigned char*)arfont.images[0].data;
    memset(io.Fonts->TexPixelsRGBA32, 0, io.Fonts->TexHeight * io.Fonts->TexWidth * 4);
    for (size_t y = 0; y < img.height; y++) //TODO check orientation
        memcpy(io.Fonts->TexPixelsRGBA32 + (img.height - y - 1) * io.Fonts->TexWidth, src + y * img.width * 4, img.width * 4);

    // Add custom rects somewhere on texture
    uint16_t max_h = 0;
    uint16_t curr_x = 0;
    uint16_t curr_y = img.height;
    for (auto& r : io.Fonts->CustomRects)
    {
        if (curr_x + r.Width > io.Fonts->TexWidth)
        {
            curr_x = 0;
            curr_y += max_h;
            max_h = 0;
        }

        r.X = curr_x;
        r.Y = curr_y;
        curr_x += r.Width;
        max_h = max(max_h, r.Height);
    }

    ImFontAtlasBuildFinish(io.Fonts);

    io.Fonts->Fonts.push_back(IM_NEW(ImFont));
    ImFont* tmp = io.Fonts->Fonts.back();
    *tmp = *io.Fonts->Fonts[0];
    tmp->Scale *= 0.55f;
    small_font = io.Fonts->Fonts.back();
    text_font = io.Fonts->Fonts[0];
}

void create_fonts(const overlay_params& params, ImFont*& small_font, ImFont*& text_font)
{
   if (ends_with(params.font_file, ".arfont") && file_exists(params.font_file))
   {
      create_font_from_mtsdf(params, small_font, text_font);
      return;
   }

   auto& io = ImGui::GetIO();
   io.Fonts->Clear();
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
      0x2018, 0x201F, // Bunch of quotation marks
      //0x0100, 0x017F, // Latin Extended-A
      //0x2103, 0x2103, // Degree Celsius
      //0x2109, 0x2109, // Degree Fahrenheit
      0,
   };
   // Load Icon file and merge to exisitng font
    ImFontConfig config;
    config.MergeMode = true;
    static const ImWchar icon_ranges[] = {
        0xf240, 0xf244, // battery icons
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
      constexpr ImWchar latin_ext_a[] { 0x0100, 0x017F, 0 };
      builder.AddRanges(latin_ext_a);
   }
   if (params.font_glyph_ranges & FG_LATIN_EXT_B) {
      constexpr ImWchar latin_ext_b[] { 0x0180, 0x024F, 0 };
      builder.AddRanges(latin_ext_b);
   }
   builder.BuildRanges(&glyph_ranges);

   bool same_font = (params.font_file == params.font_file_text || params.font_file_text.empty());
   bool same_size = (font_size == font_size_text);

   // ImGui takes ownership of the data, no need to free it
   if (!params.font_file.empty() && file_exists(params.font_file)) {
      io.Fonts->AddFontFromFileTTF(params.font_file.c_str(), font_size, nullptr, same_font && same_size ? glyph_ranges.Data : default_range);
      io.Fonts->AddFontFromMemoryCompressedBase85TTF(forkawesome_compressed_data_base85, font_size, &config, icon_ranges);
      if (params.no_small_font)
         small_font = io.Fonts->Fonts[0];
      else {
         small_font = io.Fonts->AddFontFromFileTTF(params.font_file.c_str(), font_size * 0.55f, nullptr, default_range);
         io.Fonts->AddFontFromMemoryCompressedBase85TTF(forkawesome_compressed_data_base85, font_size * 0.55f, &config, icon_ranges);
      }
   } else {
      const char* ttf_compressed_base85 = GetDefaultCompressedFontDataTTFBase85();
      io.Fonts->AddFontFromMemoryCompressedBase85TTF(ttf_compressed_base85, font_size, nullptr, default_range);
      io.Fonts->AddFontFromMemoryCompressedBase85TTF(forkawesome_compressed_data_base85, font_size, &config, icon_ranges);
      if (params.no_small_font)
         small_font = io.Fonts->Fonts[0];
      else {
         small_font = io.Fonts->AddFontFromMemoryCompressedBase85TTF(ttf_compressed_base85, font_size * 0.55f, nullptr, default_range);
         io.Fonts->AddFontFromMemoryCompressedBase85TTF(forkawesome_compressed_data_base85, font_size * 0.55f, &config, icon_ranges);
      }
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
