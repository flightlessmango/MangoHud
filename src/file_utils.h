#pragma once
#ifndef MANGOHUD_FILE_UTILS_H
#define MANGOHUD_FILE_UTILS_H

#include <string>
#include <vector>
#include <regex>
enum LS_FLAGS
{
    LS_DIRS = 0x01,
    LS_FILES = 0x02,
};

std::string read_line(const std::string& filename);
std::vector<std::string> ls(const char* root, const char* prefix = nullptr, LS_FLAGS flags = LS_DIRS);
bool file_exists(const std::string& path);
bool dir_exists(const std::string& path);
std::string read_symlink(const char * link);
std::string read_symlink(const std::string&& link);
std::string get_basename(const std::string&& path); //prefix so it doesn't conflict libgen
std::string get_exe_path();
std::string get_wine_exe_name(bool keep_ext = false);
std::string get_home_dir();
std::string get_data_dir();
std::string get_config_dir();
bool lib_loaded(const std::string& lib);
std::string remove_parentheses(const std::string&);

#endif //MANGOHUD_FILE_UTILS_H
