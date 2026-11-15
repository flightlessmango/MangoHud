#pragma once
#ifndef MANGOHUD_SYSCTL_UTILS_H
#define MANGOHUD_SYSCTL_UTILS_H

// Helpers for the sysctl(3) interface of the BSDs
#include <sys/types.h>
#include <sys/sysctl.h>
#include <cstring>
#include <string>

// Reads a fixed-size value, fails if the kernel's type has a different size
template <typename T>
static inline bool sysctl_read(const char* name, T& out)
{
    T value {};
    size_t len = sizeof(value);

    if (sysctlbyname(name, &value, &len, nullptr, 0) != 0 || len != sizeof(value))
        return false;
    out = value;
    return true;
}

static inline std::string sysctl_read_string(const char* name)
{
    size_t len = 0;

    if (sysctlbyname(name, nullptr, &len, nullptr, 0) != 0 || len == 0)
        return std::string();

    std::string value(len, '\0');
    if (sysctlbyname(name, &value[0], &len, nullptr, 0) != 0)
        return std::string();

    value.resize(strnlen(value.c_str(), len)); // drop the trailing NUL
    return value;
}

#endif // MANGOHUD_SYSCTL_UTILS_H
