#include "file_utils.h"
#include "string_utils.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <fstream>
#include <cstring>
#include <string>
#include <spdlog/spdlog.h>
#include <filesystem.h>

namespace fs = ghc::filesystem;

#ifndef PROCDIR
#define PROCDIR "/proc"
#endif

std::string read_line(const std::string& filename)
{
    std::string line;
    std::ifstream file(filename);
    if (file.fail()){
        return line;
    }
    std::getline(file, line);
    return line;
}

std::string get_basename(const std::string&& path)
{
    auto npos = path.find_last_of("/\\");
    if (npos == std::string::npos)
        return path;

    if (npos < path.size() - 1)
        return path.substr(npos + 1);
    return path;
}

#ifdef __linux__
std::vector<std::string> ls(const char* root, const char* prefix, LS_FLAGS flags)
{
    std::vector<std::string> list;
    struct dirent* dp;

    DIR* dirp = opendir(root);
    if (!dirp) {
        SPDLOG_ERROR("Error opening directory '{}': {}", root, strerror(errno));
        return list;
    }

    while ((dp = readdir(dirp))) {
        if ((prefix && !starts_with(dp->d_name, prefix))
            || !strcmp(dp->d_name, ".")
            || !strcmp(dp->d_name, ".."))
            continue;

        switch (dp->d_type) {
        case DT_LNK: {
            struct stat s;
            std::string path(root);
            if (path.back() != '/')
                path += "/";
            path += dp->d_name;

            if (stat(path.c_str(), &s))
                continue;

            if (((flags & LS_DIRS) && S_ISDIR(s.st_mode))
                || ((flags & LS_FILES) && S_ISREG(s.st_mode))) {
                list.push_back(dp->d_name);
            }
            break;
        }
        case DT_DIR:
            if (flags & LS_DIRS)
                list.push_back(dp->d_name);
            break;
        case DT_REG:
            if (flags & LS_FILES)
                list.push_back(dp->d_name);
            break;
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

std::string read_symlink(const char * link)
{
    char result[PATH_MAX] {};
    ssize_t count = readlink(link, result, PATH_MAX);
    return std::string(result, (count > 0) ? count : 0);
}

std::string read_symlink(const std::string&& link)
{
    return read_symlink(link.c_str());
}

std::string get_exe_path()
{
    return read_symlink(PROCDIR "/self/exe");
}

std::string get_wine_exe_name(bool keep_ext)
{
    const std::string exe_path = get_exe_path();
    if (!ends_with(exe_path, "wine-preloader") && !ends_with(exe_path, "wine64-preloader")) {
        return std::string();
    }

    std::string line = read_line(PROCDIR "/self/comm"); // max 16 characters though
    if (ends_with(line, ".exe", true))
    {
        auto dot = keep_ext ? std::string::npos : line.find_last_of('.');
        return line.substr(0, dot);
    }

    std::ifstream cmdline(PROCDIR "/self/cmdline");
    // Iterate over arguments (separated by NUL byte).
    while (std::getline(cmdline, line, '\0')) {
        auto n = std::string::npos;
        if (!line.empty()
            && ((n = line.find_last_of("/\\")) != std::string::npos)
            && n < line.size() - 1) // have at least one character
        {
            auto dot = keep_ext ? std::string::npos : line.find_last_of('.');
            if (dot < n)
                dot = line.size();
            return line.substr(n + 1, dot - n - 1);
        }
        else if (ends_with(line, ".exe", true))
        {
            auto dot = keep_ext ? std::string::npos : line.find_last_of('.');
            return line.substr(0, dot);
        }
    }
    return std::string();
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

bool lib_loaded(const std::string& lib) {
    fs::path path(PROCDIR "/self/map_files/");
    for (auto& p : fs::directory_iterator(path)) {
        auto file = p.path().string();
        auto sym = read_symlink(file.c_str());
        if (to_lower(sym).find(lib) != std::string::npos) {
            return true;
        }
    }
    return false;

}

std::string remove_parentheses(const std::string& text) {
    // Remove parentheses and text between them
    std::regex pattern("\\([^)]*\\)");
    return std::regex_replace(text, pattern, "");
}

std::string to_lower(const std::string& str) {
    std::string lowered = str;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return lowered;
}

#endif // __linux__
