
#pragma once

#include <cstdio>
#include "serialization.h"

namespace artery_font {

template <typename REAL, template <typename> class LIST, class BYTE_ARRAY, class STRING>
bool read(ArteryFont<REAL, LIST, BYTE_ARRAY, STRING> &font, FILE *file);

template <typename REAL, template <typename> class LIST, class BYTE_ARRAY, class STRING>
bool write(const ArteryFont<REAL, LIST, BYTE_ARRAY, STRING> &font, FILE *file);

template <typename REAL, template <typename> class LIST, class BYTE_ARRAY, class STRING>
bool readFile(ArteryFont<REAL, LIST, BYTE_ARRAY, STRING> &font, const char *filename);

template <typename REAL, template <typename> class LIST, class BYTE_ARRAY, class STRING>
bool writeFile(const ArteryFont<REAL, LIST, BYTE_ARRAY, STRING> &font, const char *filename);

}

#include "stdio-serialization.hpp"
