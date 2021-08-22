
#include "stdio-serialization.h"

namespace artery_font {

namespace internal {

inline int fileRead(void *buffer, int length, void *file) {
    return fread(buffer, 1, length, reinterpret_cast<FILE *>(file));
}

inline int fileWrite(const void *buffer, int length, void *file) {
    return fwrite(buffer, 1, length, reinterpret_cast<FILE *>(file));
}

}

template <typename REAL, template <typename> class LIST, class BYTE_ARRAY, class STRING>
bool read(ArteryFont<REAL, LIST, BYTE_ARRAY, STRING> &font, FILE *file) {
    return decode<internal::fileRead>(font, file);
}

template <typename REAL, template <typename> class LIST, class BYTE_ARRAY, class STRING>
bool write(const ArteryFont<REAL, LIST, BYTE_ARRAY, STRING> &font, FILE *file) {
    return encode<internal::fileWrite>(font, file);
}

template <typename REAL, template <typename> class LIST, class BYTE_ARRAY, class STRING>
bool readFile(ArteryFont<REAL, LIST, BYTE_ARRAY, STRING> &font, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file)
        return false;
    bool result = read(font, file);
    fclose(file);
    return result;
}

template <typename REAL, template <typename> class LIST, class BYTE_ARRAY, class STRING>
bool writeFile(const ArteryFont<REAL, LIST, BYTE_ARRAY, STRING> &font, const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file)
        return false;
    bool result = write(font, file);
    fclose(file);
    return result;
}

}
