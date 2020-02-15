#include "file_utils.h"
#include "string_utils.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/limits.h>
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
    struct dirent* dp;
    DIR* dirp = opendir(root);
    if (!dirp) {
        std::cerr << "Error opening directory '" << root << "': ";
        perror("");
        return false;
    }

    while ((dp = readdir(dirp))) {
        if ((dp->d_type == DT_LNK || dp->d_type == DT_DIR) && starts_with(dp->d_name, prefix)) {
            dest = dp->d_name;
            closedir(dirp);
            return true;
        }
    }

    closedir(dirp);
    return false;
}

bool find_folder(const std::string& root, const std::string& prefix, std::string& dest)
{
    return find_folder(root.c_str(), prefix.c_str(), dest);
}

std::vector<std::string> ls(const char* root, const char* prefix, LS_FLAGS flags)
{
    std::vector<std::string> list;
    struct dirent* dp;

    DIR* dirp = opendir(root);
    if (!dirp) {
        std::cerr << "Error opening directory '" << root << "': ";
        perror("");
        return list;
    }

    while ((dp = readdir(dirp))) {
        if ((prefix && !starts_with(dp->d_name, prefix))
            || !strcmp(dp->d_name, ".")
            || !strcmp(dp->d_name, ".."))
            continue;

        if (dp->d_type == DT_LNK) {
            struct stat s;
            std::string path(root);
            if (path.back() != '/')
                path += "/";
            path += dp->d_name;

            if (stat(path.c_str(), &s))
                continue;

            if (((flags & LS_DIRS) && S_ISDIR(s.st_mode))
                || ((flags & LS_FILES) && !S_ISDIR(s.st_mode))) {
                list.push_back(dp->d_name);
            }
        } else if (((flags & LS_DIRS) && dp->d_type == DT_DIR)
            || ((flags & LS_FILES) && dp->d_type == DT_REG)
        ) {
            list.push_back(dp->d_name);
        }
    }

    closedir(dirp);
    return list;
}

bool file_exists(const std::string& path)
{
    struct stat s;
    return !stat(path.c_str(), &s) && !S_ISDIR(s.st_mode);
}

bool dir_exists(const std::string& path)
{
    struct stat s;
    return !stat(path.c_str(), &s) && S_ISDIR(s.st_mode);
}

std::string get_exe_path()
{
    char result[PATH_MAX] {};
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    return std::string(result, (count > 0) ? count : 0);
}

std::string get_home_dir()
{
    std::string path;
    const char* p = getenv("HOME");

    if (p)
        path = p;
    return path;
}

std::string get_data_dir()
{
    const char* p = getenv("XDG_DATA_HOME");
    if (p)
        return p;

    std::string path = get_home_dir();
    if (!path.empty())
        path += "/.local/share";
    return path;
}

std::string get_config_dir()
{
    const char* p = getenv("XDG_CONFIG_HOME");
    if (p)
        return p;

    std::string path = get_home_dir();
    if (!path.empty())
        path += "/.config";
    return path;
}
