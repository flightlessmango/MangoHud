
#pragma once

#include "types.h"

namespace artery_font {

uint32 crc32Init();
uint32 crc32Update(uint32 crc, byte x);

}

#include "crc32.hpp"
