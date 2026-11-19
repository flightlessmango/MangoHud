#pragma once
#ifndef MANGOHUD_FILE_UTILS_H
#define MANGOHUD_FILE_UTILS_H

#include <string>
#include <vector>
#include <regex>
#include <array>
#include <filesystem.h>
namespace fs = ghc::filesystem;

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
std::string get_wine_exe_name(bool keep_ext = false);
std::string get_home_dir();
std::string get_data_dir();
std::string get_config_dir();

// Per-platform, implemented in file_utils_<os>.cpp
std::string get_exe_path();
std::string get_proc_comm();    // process name, 15-16 characters at most
std::string get_proc_cmdline(); // arguments separated by NUL bytes
bool is_wine_loader(const std::string& name); // name of the executable, not the path
bool lib_loaded(const std::string& lib, pid_t pid); // pid -1 is this process

std::string remove_parentheses(const std::string&);
std::string to_lower(const std::string& str);

#endif //MANGOHUD_FILE_UTILS_H
