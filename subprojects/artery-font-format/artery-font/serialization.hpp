
#include "serialization.h"

#include <cstring>
#include "crc32.h"

namespace artery_font {

namespace internal {

#define ARTERY_FONT_HEADER_TAG "ARTERY/FONT\0\0\0\0\0"
#define ARTERY_FONT_HEADER_VERSION 1u
#define ARTERY_FONT_HEADER_MAGIC_NO 0x4d276a5cu
#define ARTERY_FONT_FOOTER_MAGIC_NO 0x55ccb363u

struct ArteryFontHeader {
    char tag[16];
    uint32 magicNo;
    uint32 version;
    uint32 flags;
    uint32 realType;
    uint32 reserved[4];

    uint32 metadataFormat;
    uint32 metadataLength;
    uint32 variantCount;
    uint32 variantsLength;
    uint32 imageCount;
    uint32 imagesLength;
    uint32 appendixCount;
    uint32 appendicesLength;
    uint32 reserved2[8];
};

struct ArteryFontFooter {
    uint32 salt;
    uint32 magicNo;
    uint32 reserved[4];
    uint32 totalLength;
    uint32 checksum;
};

template <typename REAL>
struct FontVariantHeader {
    uint32 flags;
    uint32 weight;
    uint32 codepointType;
    uint32 imageType;
    uint32 fallbackVariant;
    uint32 fallbackGlyph;
    uint32 reserved[6];
    REAL metrics[32];
    uint32 nameLength;
    uint32 metadataLength;
    uint32 glyphCount;
    uint32 kernPairCount;
};

struct ImageHeader {
    uint32 flags;
    uint32 encoding;
    uint32 width, height;
    uint32 channels;
    uint32 pixelFormat;
    uint32 imageType;
    uint32 rowLength;
    sint32 orientation;
    uint32 childImages;
    uint32 textureFlags;
    uint32 reserved[3];
    uint32 metadataLength;
    uint32 dataLength;
};

struct AppendixHeader {
    uint32 metadataLength;
    uint32 dataLength;
};

template <typename REAL>
uint32 realTypeCode();

template <>
uint32 realTypeCode<float>() {
    return 0x14u;
}
template <>
uint32 realTypeCode<double>() {
    return 0x18u;
}

inline uint32 paddedLength(uint32 len) {
    if (len&0x03u)
        len += 0x04u-(len&0x03u);
    return len;
}

template <class STRING>
uint32 paddedStringLength(const STRING &str) {
    uint32 len = str.length();
    return paddedLength(len+(len > 0));
}

}

#ifndef __BIG_ENDIAN__

template <int (*READ)(void *, int, void *), typename REAL, template <typename> class LIST, class BYTE_ARRAY, class STRING>
bool decode(ArteryFont<REAL, LIST, BYTE_ARRAY, STRING> &font, void *userData) {
    uint32 totalLength = 0;
    uint32 prevLength = 0;
    uint32 checksum = crc32Init();
    byte dump[4];
    #define ARTERY_FONT_DECODE_READ(target, len) { \
        if (READ((void *) (target), (len), userData) != (len)) \
            return false; \
        totalLength += (len); \
        for (int i = 0; i < int(len); ++i) \
            checksum = crc32Update(checksum, ((const byte *) (const void *) (target))[i]); \
    }
    #define ARTERY_FONT_DECODE_REALIGN() { \
        if (totalLength&0x03u) { \
            uint32 len = 0x04u-(totalLength&0x03u); \
            ARTERY_FONT_DECODE_READ(dump, len); \
        } \
    }
    #define ARTERY_FONT_DECODE_READ_STRING(str, len) { \
        if ((len) > 0) { \
            LIST<char> characters((len)+1); \
            ARTERY_FONT_DECODE_READ((char *) characters, (len)+1); \
            ((char *) characters)[len] = '\0'; \
            (str) = STRING((const char *) characters, uint32(len)); \
            ARTERY_FONT_DECODE_REALIGN(); \
        } else \
            (str) = STRING(); \
    }
    int variantCount = 0;
    int imageCount = 0;
    int appendixCount = 0;
    uint32 variantsLength = 0;
    uint32 imagesLength = 0;
    uint32 appendicesLength = 0;
    // Read header
    {
        internal::ArteryFontHeader header;
        ARTERY_FONT_DECODE_READ(&header, sizeof(header));
        if (memcmp(header.tag, ARTERY_FONT_HEADER_TAG, sizeof(header.tag)))
            return false;
        if (header.magicNo != ARTERY_FONT_HEADER_MAGIC_NO)
            return false;
        if (header.realType != internal::realTypeCode<REAL>())
            return false;
        font.metadataFormat = (MetadataFormat) header.metadataFormat;
        ARTERY_FONT_DECODE_READ_STRING(font.metadata, header.metadataLength);
        variantCount = header.variantCount;
        imageCount = header.imageCount;
        appendixCount = header.appendixCount;
        font.variants = LIST<FontVariant<REAL, LIST, STRING> >(header.variantCount);
        font.images = LIST<Image<BYTE_ARRAY, STRING> >(header.imageCount);
        font.appendices = LIST<Appendix<BYTE_ARRAY, STRING> >(header.appendixCount);
        variantsLength = header.variantsLength;
        imagesLength = header.imagesLength;
        appendicesLength = header.appendicesLength;
    }
    prevLength = totalLength;
    // Read variants
    for (int i = 0; i < variantCount; ++i) {
        FontVariant<REAL, LIST, STRING> &variant = font.variants[i];
        internal::FontVariantHeader<REAL> header;
        ARTERY_FONT_DECODE_READ(&header, sizeof(header));
        variant.flags = header.flags;
        variant.weight = header.weight;
        variant.codepointType = (CodepointType) header.codepointType;
        variant.imageType = (ImageType) header.imageType;
        variant.fallbackVariant = header.fallbackVariant;
        variant.fallbackGlyph = header.fallbackGlyph;
        memcpy(&variant.metrics, header.metrics, sizeof(header.metrics));
        ARTERY_FONT_DECODE_READ_STRING(variant.name, header.nameLength);
        ARTERY_FONT_DECODE_READ_STRING(variant.metadata, header.metadataLength);
        variant.glyphs = LIST<Glyph<REAL> >(header.glyphCount);
        variant.kernPairs = LIST<KernPair<REAL> >(header.kernPairCount);
        ARTERY_FONT_DECODE_READ((Glyph<REAL> *) variant.glyphs, header.glyphCount*sizeof(Glyph<REAL>));
        ARTERY_FONT_DECODE_READ((KernPair<REAL> *) variant.kernPairs, header.kernPairCount*sizeof(KernPair<REAL>));
    }
    if (totalLength-prevLength != variantsLength)
        return false;
    prevLength = totalLength;
    // Read images
    for (int i = 0; i < imageCount; ++i) {
        Image<BYTE_ARRAY, STRING> &image = font.images[i];
        internal::ImageHeader header;
        ARTERY_FONT_DECODE_READ(&header, sizeof(header));
        image.flags = header.flags;
        image.encoding = (ImageEncoding) header.encoding;
        image.width = header.width;
        image.height = header.height;
        image.channels = header.channels;
        image.pixelFormat = (PixelFormat) header.pixelFormat;
        image.imageType = (ImageType) header.imageType;
        image.rawBinaryFormat.rowLength = header.rowLength;
        image.rawBinaryFormat.orientation = (ImageOrientation) header.orientation;
        image.childImages = header.childImages;
        image.textureFlags = header.textureFlags;
        ARTERY_FONT_DECODE_READ_STRING(image.metadata, header.metadataLength);
        image.data = BYTE_ARRAY(header.dataLength);
        ARTERY_FONT_DECODE_READ((unsigned char *) image.data, header.dataLength);
        ARTERY_FONT_DECODE_REALIGN();
    }
    if (totalLength-prevLength != imagesLength)
        return false;
    prevLength = totalLength;
    // Read appendices
    for (int i = 0; i < appendixCount; ++i) {
        Appendix<BYTE_ARRAY, STRING> &appendix = font.appendices[i];
        internal::AppendixHeader header;
        ARTERY_FONT_DECODE_READ(&header, sizeof(header));
        ARTERY_FONT_DECODE_READ_STRING(appendix.metadata, header.metadataLength);
        appendix.data = BYTE_ARRAY(header.dataLength);
        ARTERY_FONT_DECODE_READ((unsigned char *) appendix.data, header.dataLength);
        ARTERY_FONT_DECODE_REALIGN();
    }
    if (totalLength-prevLength != appendicesLength)
        return false;
    prevLength = totalLength;
    // Read footer
    {
        internal::ArteryFontFooter footer;
        ARTERY_FONT_DECODE_READ(&footer, sizeof(footer)-sizeof(footer.checksum));
        if (footer.magicNo != ARTERY_FONT_FOOTER_MAGIC_NO)
            return false;
        uint32 prevChecksum = checksum;
        ARTERY_FONT_DECODE_READ(&footer.checksum, sizeof(footer.checksum));
        if (footer.checksum != prevChecksum)
            return false;
        if (totalLength != footer.totalLength)
            return false;
    }
    return true;
    #undef ARTERY_FONT_DECODE_READ
    #undef ARTERY_FONT_DECODE_REALIGN
    #undef ARTERY_FONT_DECODE_READ_STRING
}

template <int (*WRITE)(const void *, int, void *), typename REAL, template <typename> class LIST, class BYTE_ARRAY, class STRING>
bool encode(const ArteryFont<REAL, LIST, BYTE_ARRAY, STRING> &font, void *userData) {
    uint32 totalLength = 0;
    uint32 checksum = crc32Init();
    const byte padding[4] = { };
    #define ARTERY_FONT_ENCODE_WRITE(data, len) { \
        if (WRITE((const void *) (data), (len), userData) != (len)) \
            return false; \
        totalLength += (len); \
        for (int i = 0; i < int(len); ++i) \
            checksum = crc32Update(checksum, ((const byte *) (const void *) (data))[i]); \
    }
    #define ARTERY_FONT_ENCODE_REALIGN() { \
        if (totalLength&0x03u) { \
            uint32 len = 0x04u-(totalLength&0x03u); \
            ARTERY_FONT_ENCODE_WRITE(padding, len); \
        } \
    }
    #define ARTERY_FONT_ENCODE_WRITE_STRING(str) { \
        uint32 len = (str).length(); \
        if ((len) > 0) { \
            ARTERY_FONT_ENCODE_WRITE((const char *) (str), (len)); \
            ARTERY_FONT_ENCODE_WRITE(padding, 1) \
            ARTERY_FONT_ENCODE_REALIGN(); \
        } \
    }
    int variantCount = 0;
    int imageCount = 0;
    int appendixCount = 0;
    // Write header
    {
        internal::ArteryFontHeader header;
        memcpy(header.tag, ARTERY_FONT_HEADER_TAG, sizeof(header.tag));
        header.magicNo = ARTERY_FONT_HEADER_MAGIC_NO;
        header.version = ARTERY_FONT_HEADER_VERSION;
        header.flags = 0;
        header.realType = internal::realTypeCode<REAL>();
        memset(header.reserved, 0, sizeof(header.reserved));
        header.metadataFormat = (uint32) font.metadataFormat;
        header.metadataLength = font.metadata.length();
        header.variantCount = variantCount = font.variants.length();
        header.variantsLength = 0;
        header.imageCount = imageCount = font.images.length();
        header.imagesLength = 0;
        header.appendixCount = appendixCount = font.appendices.length();
        header.appendicesLength = 0;
        memset(header.reserved2, 0, sizeof(header.reserved2));
        for (int i = 0; i < variantCount; ++i) {
            const FontVariant<REAL, LIST, STRING> &variant = font.variants[i];
            header.variantsLength += sizeof(internal::FontVariantHeader<REAL>);
            header.variantsLength += internal::paddedStringLength(variant.name);
            header.variantsLength += internal::paddedStringLength(variant.metadata);
            header.variantsLength += variant.glyphs.length()*sizeof(Glyph<REAL>);
            header.variantsLength += variant.kernPairs.length()*sizeof(KernPair<REAL>);
        }
        for (int i = 0; i < imageCount; ++i) {
            const Image<BYTE_ARRAY, STRING> &image = font.images[i];
            header.imagesLength += sizeof(internal::ImageHeader);
            header.imagesLength += internal::paddedStringLength(image.metadata);
            header.imagesLength += internal::paddedLength(image.data.length());
        }
        for (int i = 0; i < appendixCount; ++i) {
            const Appendix<BYTE_ARRAY, STRING> &appendix = font.appendices[i];
            header.appendicesLength += sizeof(internal::AppendixHeader);
            header.appendicesLength += internal::paddedStringLength(appendix.metadata);
            header.appendicesLength += internal::paddedLength(appendix.data.length());
        }
        ARTERY_FONT_ENCODE_WRITE(&header, sizeof(header));
        ARTERY_FONT_ENCODE_WRITE_STRING(font.metadata);
    }
    // Write variants
    for (int i = 0; i < variantCount; ++i) {
        const FontVariant<REAL, LIST, STRING> &variant = font.variants[i];
        internal::FontVariantHeader<REAL> header;
        header.flags = variant.flags;
        header.weight = variant.weight;
        header.codepointType = (uint32) variant.codepointType;
        header.imageType = (uint32) variant.imageType;
        header.fallbackVariant = variant.fallbackVariant;
        header.fallbackGlyph = variant.fallbackGlyph;
        memset(header.reserved, 0, sizeof(header.reserved));
        memcpy(header.metrics, &variant.metrics, sizeof(header.metrics));
        header.nameLength = variant.name.length();
        header.metadataLength = variant.metadata.length();
        header.glyphCount = variant.glyphs.length();
        header.kernPairCount = variant.kernPairs.length();
        ARTERY_FONT_ENCODE_WRITE(&header, sizeof(header));
        ARTERY_FONT_ENCODE_WRITE_STRING(variant.name);
        ARTERY_FONT_ENCODE_WRITE_STRING(variant.metadata);
        ARTERY_FONT_ENCODE_WRITE((const Glyph<REAL> *) variant.glyphs, header.glyphCount*sizeof(Glyph<REAL>));
        ARTERY_FONT_ENCODE_WRITE((const KernPair<REAL> *) variant.kernPairs, header.kernPairCount*sizeof(KernPair<REAL>));
    }
    // Write images
    for (int i = 0; i < imageCount; ++i) {
        const Image<BYTE_ARRAY, STRING> &image = font.images[i];
        internal::ImageHeader header;
        header.flags = image.flags;
        header.encoding = (uint32) image.encoding;
        header.width = image.width;
        header.height = image.height;
        header.channels = image.channels;
        header.pixelFormat = (uint32) image.pixelFormat;
        header.imageType = (uint32) image.imageType;
        header.rowLength = image.rawBinaryFormat.rowLength;
        header.orientation = (sint32) image.rawBinaryFormat.orientation;
        header.childImages = image.childImages;
        header.textureFlags = image.textureFlags;
        memset(header.reserved, 0, sizeof(header.reserved));
        header.metadataLength = image.metadata.length();
        header.dataLength = image.data.length();
        ARTERY_FONT_ENCODE_WRITE(&header, sizeof(header));
        ARTERY_FONT_ENCODE_WRITE_STRING(image.metadata);
        ARTERY_FONT_ENCODE_WRITE((const unsigned char *) image.data, header.dataLength);
        ARTERY_FONT_ENCODE_REALIGN();
    }
    // Write appendices
    for (int i = 0; i < appendixCount; ++i) {
        const Appendix<BYTE_ARRAY, STRING> &appendix = font.appendices[i];
        internal::AppendixHeader header;
        header.metadataLength = appendix.metadata.length();
        header.dataLength = appendix.data.length();
        ARTERY_FONT_ENCODE_WRITE(&header, sizeof(header));
        ARTERY_FONT_ENCODE_WRITE_STRING(appendix.metadata);
        ARTERY_FONT_ENCODE_WRITE((const unsigned char *) appendix.data, header.dataLength);
        ARTERY_FONT_ENCODE_REALIGN();
    }
    // Write footer
    {
        internal::ArteryFontFooter footer;
        footer.salt = 0;
        footer.magicNo = ARTERY_FONT_FOOTER_MAGIC_NO;
        memset(footer.reserved, 0, sizeof(footer.reserved));
        footer.totalLength = totalLength+sizeof(footer);
        ARTERY_FONT_ENCODE_WRITE(&footer, sizeof(footer)-sizeof(footer.checksum));
        footer.checksum = checksum;
        ARTERY_FONT_ENCODE_WRITE(&footer.checksum, sizeof(footer.checksum));
    }
    return true;
    #undef ARTERY_FONT_ENCODE_WRITE
    #undef ARTERY_FONT_ENCODE_REALIGN
    #undef ARTERY_FONT_ENCODE_WRITE_STRING
}

#endif

#undef ARTERY_FONT_HEADER_TAG
#undef ARTERY_FONT_HEADER_VERSION
#undef ARTERY_FONT_HEADER_MAGIC_NO
#undef ARTERY_FONT_FOOTER_MAGIC_NO

}
