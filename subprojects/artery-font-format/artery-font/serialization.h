
#pragma once

#include "types.h"
#include "enums.h"
#include "structures.h"

namespace artery_font {

template <int (*READ)(void *, int, void *), typename REAL, template <typename> class LIST, class BYTE_ARRAY, class STRING>
bool decode(ArteryFont<REAL, LIST, BYTE_ARRAY, STRING> &font, void *userData);

template <int (*WRITE)(const void *, int, void *), typename REAL, template <typename> class LIST, class BYTE_ARRAY, class STRING>
bool encode(const ArteryFont<REAL, LIST, BYTE_ARRAY, STRING> &font, void *userData);

}

#include "serialization.hpp"
