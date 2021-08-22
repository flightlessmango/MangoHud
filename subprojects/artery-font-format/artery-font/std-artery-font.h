
#pragma once

#include <vector>
#include <string>
#include "artery-font.h"

namespace artery_font {

template <typename T>
class StdList {

public:
    std::vector<T> vector;

    StdList() { }
    explicit StdList(int length) : vector((size_t) length) { }
    int length() const { return (int) vector.size(); }
    explicit operator T *() { return vector.data(); }
    explicit operator const T *() const { return vector.data(); }
    T & operator[](int index) { return vector[index]; }
    const T & operator[](int index) const { return vector[index]; }

};

class StdString {

public:
    std::string string;

    StdString() { }
    StdString(const char *characters, int length) : string(characters, (size_t) length) { }
    int length() const { return (int) string.size(); }
    explicit operator const char *() const { return string.c_str(); }

};

typedef StdList<unsigned char> StdByteArray;

template <typename REAL>
using StdArteryFont = ArteryFont<REAL, StdList, StdByteArray, StdString>;
template <typename REAL>
using StdFontVariant = FontVariant<REAL, StdList, StdString>;
using StdImage = Image<StdByteArray, StdString>;
using StdAppendix = Appendix<StdByteArray, StdString>;

}
