#pragma once
#ifndef MANGOHUD_FILE_UTILS_H
#define MANGOHUD_FILE_UTILS_H

#include <cinttypes>
#include <filesystem.h>
#include <string>
#include <vector>

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


namespace detail {

    // using template specialization to let the compiler figure it out
    template<typename T> const char* type_to_fmt();
    template<> inline const char* type_to_fmt<int>() { return "%d"; }
    template<> inline const char* type_to_fmt<int64_t>() { return "%" PRIi64; }
    template<> inline const char* type_to_fmt<uint64_t>() { return "%" SCNi64; }
    template<> inline const char* type_to_fmt<float>() { return "%f"; }
    template<> inline const char* type_to_fmt<double>() { return "%lf"; }

}

/** Read a single value from sysfs file */
template<typename ValueType>
ValueType read_as(FILE* f, ValueType d = 0) {
    rewind(f);
    fflush(f);
    ValueType v;
    if (fscanf(f, detail::type_to_fmt<ValueType>(), &v) != 1)
        return d;
    return v;
}

#endif //MANGOHUD_FILE_UTILS_H
