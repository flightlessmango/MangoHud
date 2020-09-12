#include "file_utils.h"
#include "string_utils.h"
#include <iostream>
#include <fstream>
#include <cstring>

std::string read_line(const std::string& filename)
{
    std::string line;
    std::ifstream file(filename);
    std::getline(file, line);
    return line;
}

bool find_folder(const char* root, const char* prefix, std::string& dest)
{
    return false;
}

bool find_folder(const std::string& root, const std::string& prefix, std::string& dest)
{
    return find_folder(root.c_str(), prefix.c_str(), dest);
}

std::vector<std::string> ls(const char* root, const char* prefix, LS_FLAGS flags)
{
    std::vector<std::string> list;
    return list;
}

bool file_exists(const std::string& path)
{
    return false;
}

bool dir_exists(const std::string& path)
{
    return false;
}

std::string get_exe_path()
{
    return std::string();
}

bool get_wine_exe_name(std::string& name, bool keep_ext)
{
    return false;
}

std::string get_home_dir()
{
    std::string path;
    return path;
}

std::string get_data_dir()
{
    std::string path;
    return path;
}

std::string get_config_dir()
{
    std::string path;
    return path;
}
