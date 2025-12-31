#include <fstream>
#include <algorithm>
#include <spdlog/spdlog.h>
#include "helpers.hpp"

std::string read_line(const std::string& filename) {
    std::string line;
    std::ifstream file(filename);

    if (file.fail())
        return line;

    std::getline(file, line);
    return line;
}

bool ends_with(std::string s1, std::string s2, bool ignore_case) {
    if (s1.size() < s2.size())
        return false;

    if (ignore_case) {
        std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
        std::transform(s2.begin(), s2.end(), s2.begin(), ::tolower);
    }

    size_t pos = s1.size() - s2.size();
    return (s1.rfind(s2, pos) == pos);
}

uint64_t try_stoull(const std::string& str) {
    try {
        uint64_t val = std::stoull(str);
        return val;
    } catch(...) {
        SPDLOG_DEBUG("failed to convert \"{}\" to uint64_t", str);
    }

    return 0;
}
