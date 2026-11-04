#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <spdlog/spdlog.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "memory.h"
#include "file_utils.h"
#include "hud_elements.h"
#include "string_utils.h"

float memused, memmax, swapused;
int mem_temp = -1;
uint64_t proc_mem_resident, proc_mem_shared, proc_mem_virt;
uint64_t cgroup_mem_used;

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

void update_mem_temp() {
    static bool inited = false;
    static std::vector<std::ifstream> mem_temp_files;

    if (!inited) {
        inited = true;
        std::string path = "/sys/class/hwmon/";
        auto dirs = ls(path.c_str(), "hwmon", LS_DIRS);
        for (auto &dir : dirs) {
            if (read_line(path + dir + "/name") == "spd5118")
                mem_temp_files.emplace_back(path + dir + "/temp1_input");
        }
        if (mem_temp_files.empty())
            SPDLOG_ERROR("failed to find known ram temp sensors");
    }

    if (mem_temp_files.empty())
        return;

    int temp = 0;
    for (auto &file : mem_temp_files) {
        int _temp;
        file.clear();
        file.seekg(0);
        if ((file >> _temp) && _temp > temp)
            temp = _temp;
    }
    mem_temp = temp / 1000;
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

// systemd app units, app.slice/app-*.scope or app-*.service
static std::string app_cgroup(const std::string& proc)
{
    std::ifstream file(proc + "/cgroup");

    for (std::string line; std::getline(file, line);) {
        if (!starts_with(line, "0::") || ends_with(line, " (deleted)"))
            continue;

        auto path = line.substr(3);
        auto slice = path.find("/app.slice/");
        if (slice == std::string::npos)
            continue;

        // match whole unit names, not e.g. app-dbus-*.slice
        std::istringstream units(path.substr(slice + strlen("/app.slice/")));
        for (std::string unit; std::getline(units, unit, '/');) {
            if (starts_with(unit, "app-") &&
                (ends_with(unit, ".scope") || ends_with(unit, ".service")))
                return "/sys/fs/cgroup" + path;
        }
    }

    return {};
}

// anon + shmem: what reclaim can't drop, plus GPU buffers on shmem-backed drivers
static uint64_t read_cgroup_mem(const std::string& cgroup)
{
    std::ifstream file(cgroup + "/memory.stat");

    uint64_t anon = 0, shmem = 0;
    std::string key;
    uint64_t val;
    while (file >> key >> val) {
        if (key == "anon")
            anon = val;
        else if (key == "shmem")
            shmem = val;
    }

    return anon + shmem;
}

void update_cgroup_mem()
{
    static std::string cgroup;

    auto gs_pid = HUDElements.g_gamescopePid;
    std::string proc = "/proc/" + (gs_pid < 1 ? std::string("self") : std::to_string(gs_pid));

    // resolve every time, the process can move between cgroups
    auto path = app_cgroup(proc);
    if (!path.empty() && path != cgroup)
        SPDLOG_DEBUG("cgroup memory from {}", path);
    cgroup = path;

    cgroup_mem_used = cgroup.empty() ? 0 : read_cgroup_mem(cgroup);
}
