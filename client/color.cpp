#include <vulkan/vulkan.h>
#include <imgui.h>
#include <cmath>

enum overlay_transfer_function {
   NONE = 0,
   SRGB = (1 << 0),
   PQ = (1 << 1), /* HDR10 ST2084 */
   HLG = (1 << 2) /* HDR10 */
};

int transfer_function;

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

static float LinearToPQ(float in)
{
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;
    /* target 200 cd/m^2 as our maximum rather than 10000 cd/m^2 */
    const float targetL = 200.f;
    const float maxL = 10000.0f;

    in = powf(in * (targetL / maxL), m1);
    in = (c1 + c2 * in) / (1.0f + c3 * in);
    return powf(in, m2);
}

static double dot(const ImVec4& a, const ImVec4& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

static ImVec4 SRGBtoBT2020(ImVec4 col)
{
    const ImVec4 to2020[4] = {
        {0.627392, 0.32903, 0.0432691, 0.0},
        {0.0691229, 0.9195232, 0.0113204, 0.0},
        {0.0164229, 0.088042, 0.8956166, 0.0},
        {0.0, 0.0, 0.0, 1.0}
    };

    col.x = dot(to2020[0], col);
    col.y = dot(to2020[1], col);
    col.z = dot(to2020[2], col);
    col.w = dot(to2020[3], col);

    return col;
}

static ImVec4 LinearToPQ(ImVec4 col)
{
    col = SRGBtoBT2020(col);

    col.x = LinearToPQ(col.x);
    col.y = LinearToPQ(col.y);
    col.z = LinearToPQ(col.z);

    return col;
}

static float LinearToHLG(float in)
{
    const float a = 0.17883277f;
    const float b = 0.28466892f;
    const float c = 0.55991073f;

    if (in <= 1.0f/12.0f)
        return sqrtf(3.0f * in);
    else
        return a * logf(12.0f * in - b) + c;
}

static ImVec4 LinearToHLG(ImVec4 col)
{
    col = SRGBtoBT2020(col);

    col.x = LinearToHLG(col.x);
    col.y = LinearToHLG(col.y);
    col.z = LinearToHLG(col.z);

    return col;
}

static void convert_colors(const struct overlay_params& params)
{
    colors.update = false;
    auto convert = [&params](unsigned color) -> ImVec4 {
        ImVec4 fc = ImGui::ColorConvertU32ToFloat4(color);
        fc.w = alpha;
        if (colors.convert)
        {
            switch(transfer_function)
            {
                case PQ:
                    fc = SRGBToLinear(fc);
                    return LinearToPQ(fc);
                case HLG:
                    fc = SRGBToLinear(fc);
                    return LinearToHLG(fc);
                case SRGB:
                    return SRGBToLinear(fc);
                default: break;
            }
        }
        return fc;
    };

    HUDElements.colors.cpu = convert(params.cpu_color);
    HUDElements.colors.gpu = convert(params.gpu_color);
    HUDElements.colors.vram = convert(params.vram_color);
    HUDElements.colors.ram = convert(params.ram_color);
    HUDElements.colors.engine = convert(params.engine_color);
    HUDElements.colors.io = convert(params.io_color);
    HUDElements.colors.frametime = convert(params.frametime_color);
    HUDElements.colors.background = convert(params.background_color);
    HUDElements.colors.text = convert(params.text_color);
    HUDElements.colors.media_player = convert(params.media_player_color);
    HUDElements.colors.wine = convert(params.wine_color);
    HUDElements.colors.horizontal_separator = convert(params.horizontal_separator_color);
    HUDElements.colors.battery = convert(params.battery_color);
    HUDElements.colors.gpu_load_low = convert(params.gpu_load_color[0]);
    HUDElements.colors.gpu_load_med = convert(params.gpu_load_color[1]);
    HUDElements.colors.gpu_load_high = convert(params.gpu_load_color[2]);
    HUDElements.colors.cpu_load_low = convert(params.cpu_load_color[0]);
    HUDElements.colors.cpu_load_med = convert(params.cpu_load_color[1]);
    HUDElements.colors.cpu_load_high = convert(params.cpu_load_color[2]);
    HUDElements.colors.fps_value_low = convert(params.fps_color[0]);
    HUDElements.colors.fps_value_med = convert(params.fps_color[1]);
    HUDElements.colors.fps_value_high = convert(params.fps_color[2]);
    HUDElements.colors.text_outline = convert(params.text_outline_color);
    HUDElements.colors.network = convert(params.network_color);

    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_PlotLines] = convert(params.frametime_color);
    style.Colors[ImGuiCol_PlotHistogram] = convert(params.frametime_color);
    style.Colors[ImGuiCol_WindowBg]  = convert(params.background_color);
    style.Colors[ImGuiCol_Text] = convert(params.text_color);
    style.CellPadding.y = params.cellpadding_y * real_font_size.y;
    style.WindowRounding = params.round_corners;
    style.AntiAliasedLines = false;
}

static void convert_colors(bool do_conv, const struct overlay_params& params)
{
    HUDElements.colors.convert = do_conv;
    convert_colors(params);
}

static void convert_colors_vk(VkFormat format, VkColorSpaceKHR colorspace, struct swapchain_stats& sw_stats, struct overlay_params& params)
{
   /* TODO: Support more colorspacess */
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
      /* use no conversion for rest of the colorspaces */
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
            params.transfer_function = SRGB;
            break;
         default: break;
      }
   }

   HUDElements.convert_colors(params.transfer_function != NONE, params);
}
