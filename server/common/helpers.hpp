#pragma once

#include <string>
#include <cstdint>

std::string read_line(const std::string& filename);
bool ends_with(std::string s1, std::string s2, bool ignore_case = false);
uint64_t try_stoull(const std::string& str);
