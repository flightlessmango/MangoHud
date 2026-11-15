#include "file_utils.h"
#include "string_utils.h"
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>

#ifndef PROCDIR
#define PROCDIR "/proc"
#endif

std::string get_exe_path()
{
    return read_symlink(PROCDIR "/self/exe");
}

std::string get_proc_comm()
{
    return read_line(PROCDIR "/self/comm");
}

std::string get_proc_cmdline()
{
    std::ifstream file(PROCDIR "/self/cmdline");
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool is_wine_loader(const std::string& name)
{
    return ends_with(name, "wine-preloader") || ends_with(name, "wine64-preloader");
}

bool lib_loaded(const std::string& lib, pid_t pid) {

   std::string who = pid != -1 ? std::to_string(pid) : "self";
   auto paths = { fs::path("/proc") / who / "map_files",
            fs::path("/proc") / who / "fd" };
    for (const auto& path : paths) {
        try {
            std::error_code iterator_ec;
            for (fs::directory_iterator it(path, iterator_ec), end;
                 !iterator_ec && it != end; it.increment(iterator_ec)) {
                const auto& p = *it;
                auto file = p.path().string();
                auto sym = read_symlink(file.c_str());
                if (to_lower(sym).find(lib) != std::string::npos) {
                    return true;
                }
            }
            if (iterator_ec)
                SPDLOG_DEBUG("Failed to iterate {}: {}", fs::path(path).string(), iterator_ec.message());
        }
        catch (const fs::filesystem_error& e) {
            SPDLOG_DEBUG("failed to iterate {}: {}", path.string(), e.what());
        }
    }
    return false;
}
