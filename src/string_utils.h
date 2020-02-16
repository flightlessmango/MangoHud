#pragma once
#include <string>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <locale>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

// https://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring#217605
// trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}

// trim from start (copying)
static inline std::string ltrim_copy(std::string s) {
    ltrim(s);
    return s;
}

// trim from end (copying)
static inline std::string rtrim_copy(std::string s) {
    rtrim(s);
    return s;
}

// trim from both ends (copying)
static inline std::string trim_copy(std::string s) {
    trim(s);
    return s;
}

static bool starts_with(const std::string& s,  const char *t) {
    return s.rfind(t, 0) == 0;
}

template<typename T>
static std::string itox(T i) {
    std::stringstream ss;
    ss << "0x"
        << std::setfill ('0') << std::setw(sizeof(T) * 2)
        << std::hex << i;
    return ss.str();
}

static bool try_stoi(int& val, const std::string& str, std::size_t* pos = 0, int base = 10)
{
    try {
        val = std::stoi(str, pos, base);
        return true;
    } catch (std::invalid_argument& e) {
        std::cerr << __func__ << ": invalid argument: '" << str << "'" << std::endl;
    }
    return false;
}

static bool try_stoull(unsigned long long& val, const std::string& str, std::size_t* pos = 0, int base = 10)
{
    try {
        val = std::stoull(str, pos, base);
        return true;
    } catch (std::invalid_argument& e) {
        std::cerr << __func__ << ": invalid argument: '" << str << "'" << std::endl;
    }
    return false;
}

#pragma GCC diagnostic pop
