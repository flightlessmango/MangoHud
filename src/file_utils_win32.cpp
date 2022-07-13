#include "file_utils.h"
#include "string_utils.h"
#include <fstream>
#include <string>

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

std::string get_wine_exe_name(bool keep_ext)
{
    return std::string();
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
