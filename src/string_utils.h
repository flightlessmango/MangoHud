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

static bool try_stoi(int& val, const std::string& str, std::size_t* pos = 0, int base = 10)
{
    try {
        val = std::stoi(str, pos, base);
        return true;
    } catch (std::invalid_argument& e) {
#ifndef NDEBUG
        std::cerr << __func__ << ": invalid argument: '" << str << "'" << std::endl;
#endif
    }
    return false;
}

static bool try_stoull(unsigned long long& val, const std::string& str, std::size_t* pos = 0, int base = 10)
{
    try {
        val = std::stoull(str, pos, base);
        return true;
    } catch (std::invalid_argument& e) {
#ifndef NDEBUG
        std::cerr << __func__ << ": invalid argument: '" << str << "'" << std::endl;
#endif
    }
    return false;
}

static float parse_float(const std::string& s, std::size_t* float_len = nullptr){
    enum class State {
        Start,
        Sign,
        IntegerPart,
        DecimalPoint,
        FractionalPart,
        End
    };
    if(s.size() == 0) return; 
    
    float sign = 1.0f;
    std::string integer_part;
    std::string fractional_part;

    auto part_begin = s.begin();
    State state = State::Start;
    auto it = s.begin();
    for(; (it != s.end()) && (state != State::End); it++){
        switch(state){
        case State::Start: {
            if(std::isspace(*it)){
                // Ignore whitespace
                continue;
            }
            else if (std::isdigit(*it)){
                part_begin = it;
                state = State::IntegerPart;
            }
            else if(*it == '-') {
                sign = -1.0f;
                state = State::Sign;
            }
            else if(*it == '+') {
                state = State::Sign;
            }
            else {
                // Invalid character at the beginning
                throw std::invalid_argument("invalid char at beginning");
            }
        } break;
        case State::Sign: {
            if(std::isdigit(*it)){
                part_begin = it;
                state = State::IntegerPart;
            }
            else {
                // Invalid character after [+-]
                throw std::invalid_argument("invalid char after [+-]");
            }
        } break;
        case State::IntegerPart: {
            if(std::isdigit(*it)) {
                continue;
            }
            else if (*it == '.') {
                integer_part = std::string(part_begin, it);
                state = State::DecimalPoint;
            }
            else {
                integer_part = std::string(part_begin, it);
                state = State::End;
            }
        } break;
        case State::DecimalPoint: {
            if(std::isdigit(*it)){
                part_begin = it;
                state = State::FractionalPart;
            }
            else {
                // Invalid character after the decimal point
                throw std::invalid_argument("invalid char after decimal point");
            }
        } break;
        case State::FractionalPart: {
            if(std::isdigit(*it)){
                continue;
            }
            else{
                fractional_part = std::string(part_begin, it);
                state = State::End;
            }
        } break;
        };
    }
    
    // Check if we reached the end
    if(it == s.end()) {
        if(state == State::IntegerPart){
            integer_part = std::string(part_begin, it);
        }
        else if(state == State::FractionalPart) {
            fractional_part = std::string(part_begin, it);
        }
        else {
            throw std::invalid_argument("invalid end-of-string");
        }
    }

    float result = static_cast<float>(std::stoull(integer_part));
    if(not fractional_part.empty()){
        float frac_part = static_cast<float>(std::stoull(fractional_part));
        while(frac_part > 1.0f) frac_part /= 10;
        result += frac_part;
    }

    if(float_len != nullptr) {
        *float_len = (it - s.begin());
    }
    return result;
}

#pragma GCC diagnostic pop
