#include <string>
#include <vector>

std::string get_data_dir()
{
    return std::string();
}

std::string get_config_dir()
{
    return std::string();
}

std::string get_exe_path()
{
    return std::string();
}

std::string get_wine_exe_name(bool keep_ext)
{
    return std::string();
}

enum LS_FLAGS
{
    LS_DIRS = 0x01,
    LS_FILES = 0x02,
};

std::vector<std::string> ls(const char* root, const char* prefix, LS_FLAGS flags)
{
    std::vector<std::string> list;
    return list;
}

bool file_exists(const std::string& path){
   return "";
}