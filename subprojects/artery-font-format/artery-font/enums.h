
#pragma once

namespace artery_font {

enum FontFlags {
    FONT_BOLD = 0x01,
    FONT_LIGHT = 0x02,
    FONT_EXTRA_BOLD = 0x04,
    FONT_CONDENSED = 0x08,
    FONT_ITALIC = 0x10,
    FONT_SMALL_CAPS = 0x20,
    FONT_ICONOGRAPHIC = 0x0100,
    FONT_SANS_SERIF = 0x0200,
    FONT_SERIF = 0x0400,
    FONT_MONOSPACE = 0x1000,
    FONT_CURSIVE = 0x2000
};

enum CodepointType {
    CP_UNSPECIFIED = 0,
    CP_UNICODE = 1,
    CP_INDEXED = 2,
    CP_ICONOGRAPHIC = 14
};

enum MetadataFormat {
    METADATA_NONE = 0,
    METADATA_PLAINTEXT = 1,
    METADATA_JSON = 2
};

enum ImageType {
    IMAGE_NONE = 0,
    IMAGE_SRGB_IMAGE = 1,
    IMAGE_LINEAR_MASK = 2,
    IMAGE_MASKED_SRGB_IMAGE = 3,
    IMAGE_SDF = 4,
    IMAGE_PSDF = 5,
    IMAGE_MSDF = 6,
    IMAGE_MTSDF = 7,
    IMAGE_MIXED_CONTENT = 255
};

enum PixelFormat {
    PIXEL_UNKNOWN = 0,
    PIXEL_BOOLEAN1 = 1,
    PIXEL_UNSIGNED8 = 8,
    PIXEL_FLOAT32 = 32
};

enum ImageEncoding {
    IMAGE_UNKNOWN_ENCODING = 0,
    IMAGE_RAW_BINARY = 1,
    IMAGE_BMP = 4,
    IMAGE_TIFF = 5,
    IMAGE_PNG = 8,
    IMAGE_TGA = 9
};

enum ImageOrientation {
    ORIENTATION_TOP_DOWN = 1,
    ORIENTATION_BOTTOM_UP = -1
};

}
