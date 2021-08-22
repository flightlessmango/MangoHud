
#pragma once

#include "types.h"
#include "enums.h"

namespace artery_font {

template <typename REAL>
struct Glyph {
    uint32 codepoint;
    uint32 image;
    struct {
        REAL l, b, r, t;
    } planeBounds, imageBounds;
    struct {
        REAL h, v;
    } advance;
};

template <typename REAL>
struct KernPair {
    uint32 codepoint1, codepoint2;
    struct {
        REAL h, v;
    } advance;
};

template <typename REAL, template <typename> class LIST, class STRING>
struct FontVariant {
    uint32 flags;
    uint32 weight;
    CodepointType codepointType;
    ImageType imageType;
    uint32 fallbackVariant;
    uint32 fallbackGlyph;
    struct Metrics {
        // In pixels:
        REAL fontSize;
        REAL distanceRange;
        // Proportional to font size:
        REAL emSize;
        REAL ascender, descender;
        REAL lineHeight;
        REAL underlineY, underlineThickness;
        REAL reserved[24];
    } metrics;
    STRING name;
    STRING metadata;
    LIST<Glyph<REAL> > glyphs;
    LIST<KernPair<REAL> > kernPairs;
};

template <class BYTE_ARRAY, class STRING>
struct Image {
    uint32 flags;
    ImageEncoding encoding;
    uint32 width, height;
    uint32 channels;
    PixelFormat pixelFormat;
    ImageType imageType;
    struct {
        uint32 rowLength;
        ImageOrientation orientation;
    } rawBinaryFormat;
    uint32 childImages;
    uint32 textureFlags;
    STRING metadata;
    BYTE_ARRAY data;
};

template <class BYTE_ARRAY, class STRING>
struct Appendix {
    STRING metadata;
    BYTE_ARRAY data;
};

template <typename REAL, template <typename> class LIST, class BYTE_ARRAY, class STRING>
struct ArteryFont {
    MetadataFormat metadataFormat;
    STRING metadata;
    LIST<FontVariant<REAL, LIST, STRING> > variants;
    LIST<Image<BYTE_ARRAY, STRING> > images;
    LIST<Appendix<BYTE_ARRAY, STRING> > appendices;
};

}
