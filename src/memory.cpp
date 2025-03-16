#include <spdlog/spdlog.h>
#include <map>
#include <fstream>
#include <string>
#include <unistd.h>
#include <array>

#include "memory.h"
#include "hud_elements.h"

float memused, memmax, swapused;
uint64_t proc_mem_resident, proc_mem_shared, proc_mem_virt;

void update_meminfo() {
    std::ifstream file("/proc/meminfo");
    std::map<std::string, float> meminfo;

    if (!file.is_open()) {
        SPDLOG_ERROR("can't open /proc/meminfo");
        return;
    }

    for (std::string line; std::getline(file, line);) {
        auto key = line.substr(0, line.find(":"));
        auto val = line.substr(key.length() + 2);
        meminfo[key] = std::stoull(val) / 1024.f / 1024.f;
    }

    memmax = meminfo["MemTotal"];
    memused = meminfo["MemTotal"] - meminfo["MemAvailable"];
    swapused = meminfo["SwapTotal"] - meminfo["SwapFree"];
}

void update_procmem()
{
    auto page_size = sysconf(_SC_PAGESIZE);
    if (page_size < 0) page_size = 4096;

    std::string f = "/proc/";

    {
        auto gs_pid = HUDElements.g_gamescopePid;
        f += gs_pid < 1 ? "self" : std::to_string(gs_pid);
        f += "/statm";
    }

    std::ifstream file(f);

    if (!file.is_open()) {
        SPDLOG_ERROR("can't open {}", f);
        return;
    }

    size_t last_idx = 0;
    std::string line;
    std::getline(file, line);

    if (line.empty())
        return;

    std::array<uint64_t, 3> meminfo;

    for (auto i = 0; i < 3; i++) {
        auto idx = line.find(" ", last_idx);
        auto val = line.substr(last_idx, idx);

        meminfo[i] = std::stoull(val) * page_size;
        last_idx = idx + 1;
    }

    proc_mem_virt = meminfo[0];
    proc_mem_resident = meminfo[1];
    proc_mem_shared = meminfo[2];
}
