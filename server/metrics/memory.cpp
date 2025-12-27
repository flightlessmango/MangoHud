#include <map>
#include <fstream>
#include <unistd.h>
#include "spdlog/spdlog.h"
#include "memory.hpp"

std::map<std::string, float> get_ram_info() {
    std::map<std::string, float> ret = {
        { "total"       , 0.f },
        { "used"        , 0.f },
        { "swap_used"   , 0.f } 
    };

    std::ifstream file("/proc/meminfo");
    std::map<std::string, float> meminfo;

    if (!file.is_open()) {
        SPDLOG_ERROR("can't open /proc/meminfo");
        return ret;
    }

    for (std::string line; std::getline(file, line);) {
        std::string key = line.substr(0, line.find(":"));
        std::string val = line.substr(key.length() + 2);
        meminfo[key] = std::stoull(val) / 1024.f / 1024.f;
    }

    ret["total"]        = meminfo["MemTotal"];
    ret["used"]         = meminfo["MemTotal"]  - meminfo["MemAvailable"];
    ret["swap_used"]    = meminfo["SwapTotal"] - meminfo["SwapFree"];

    return ret;
}

std::map<std::string, float> get_process_memory(pid_t pid)
{
    std::map<std::string, float> ret = {
        { "resident", 0 },
        { "shared"  , 0 },
        { "virtual" , 0 } 
    };

    // https://man7.org/linux/man-pages/man3/sysconf.3.html
    // PAGESIZE - _SC_PAGESIZE
    // Size of a page in bytes.  Must not be less than 1.
    long page_size = sysconf(_SC_PAGESIZE);

    std::string f = "/proc/" + std::to_string(pid) + "/statm";

    std::ifstream file(f);

    if (!file.is_open()) {
        SPDLOG_ERROR("can't open {}", f);
        return ret;
    }

    size_t last_idx = 0;
    std::string line;
    std::getline(file, line);

    if (line.empty())
        return ret;

    std::array<uint64_t, 3> meminfo;

    for (auto i = 0; i < 3; i++) {
        auto idx = line.find(" ", last_idx);
        auto val = line.substr(last_idx, idx);

        meminfo[i] = std::stoull(val) * page_size;
        last_idx = idx + 1;
    }

    ret["resident"] = meminfo[1];
    ret["shared"]   = meminfo[2];
    ret["virtual"]  = meminfo[0];

    return ret;
}
