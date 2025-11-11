#pragma once
#ifndef MANGOHUD_FILE_UTILS_H
#define MANGOHUD_FILE_UTILS_H

#include <array>
#include <cinttypes>
#include <filesystem.h>
#include <regex>
#include <string>
#include <vector>

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
std::string get_exe_path();
std::string get_wine_exe_name(bool keep_ext = false);
std::string get_home_dir();
std::string get_data_dir();
std::string get_config_dir();
bool lib_loaded(const std::string& lib, pid_t pid);
std::string remove_parentheses(const std::string&);
std::string to_lower(const std::string& str);


/** Read a single value from sysfs file */
static inline int read_as_int(FILE* f, int d = 0) {
    rewind(f);
    fflush(f);
    int v;
    if (fscanf(f, "%d" , &v) != 1)
        return d;
    return v;
}

static inline int64_t read_as_int64(FILE* f, int64_t d = 0) {
    rewind(f);
    fflush(f);
    int64_t v;
    if (fscanf(f, "%" PRId64, &v) != 1)
        return d;
    return v;
}

#endif //MANGOHUD_FILE_UTILS_H
