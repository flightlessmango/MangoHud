#pragma once
#ifndef MANGOHUD_STRING_UTILS_H
#define MANGOHUD_STRING_UTILS_H

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

static bool ends_with(const std::string& s,  const char *t, bool icase = false) {
    std::string s0(s);
    std::string s1(t);

    if (s0.size() < s1.size())
        return false;

    if (icase) {
        std::transform(s0.begin(), s0.end(), s0.begin(), ::tolower);
        std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
    }

    size_t pos = s0.size() - s1.size();
    return (s0.rfind(s1, pos) == pos);
}

template<typename T>
static std::string itox(T i) {
    std::stringstream ss;
    ss << "0x"
        << std::setfill ('0') << std::setw(sizeof(T) * 2)
        << std::hex << i;
    return ss.str();
}

static bool try_stoi(int& val, const std::string& str)
{
    if (sscanf(str.c_str(), "%d", &val) == 1)
        return true;
    return false;
}

static bool try_stoull(unsigned long long& val, const std::string& str)
{
    if (sscanf(str.c_str(), "%llu", &val) == 1)
        return true;
    return false;
}

static float parse_float(const std::string& s, std::size_t* float_len = nullptr){
    std::stringstream ss(s);
    ss.imbue(std::locale::classic());
    float ret;
    ss >> ret;
    if(ss.fail()) throw std::invalid_argument("parse_float: Not a float");
    if(float_len != nullptr){
        auto pos = ss.tellg();
        if(ss.fail()) *float_len = s.size();
        else *float_len = pos;
    }
    return ret;
}

#pragma GCC diagnostic pop

#endif //MANGOHUD_STRING_UTILS_H
