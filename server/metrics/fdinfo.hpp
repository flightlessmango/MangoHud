#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <chrono>

typedef std::map<std::string, std::string> fdinfo_data;
typedef std::chrono::time_point<std::chrono::steady_clock> chrono_timer;

class FDInfoBase {
private:
    std::vector<std::ifstream> fds_streams;
    chrono_timer last_init;

    std::vector<std::string> find_fds();
    void open_fds(const std::vector<std::string>& fds);

public:
    const std::string drm_node;
    const pid_t pid;

    FDInfoBase(const std::string& drm_node, const pid_t pid);
    std::vector<fdinfo_data> fds_data;

    void init();
    void poll();
};

struct FDInfoWrapper {
    std::mutex pids_mutex;
    std::map<pid_t, FDInfoBase> pids;
    const std::string drm_node;

    explicit FDInfoWrapper(const std::string& drm_node) : drm_node(drm_node) {}

    void add_pid(pid_t pid);
    void poll_all();
    float get_memory_used(pid_t pid, const std::string& key);
    uint64_t get_gpu_time(pid_t pid, const std::string& key);
};

struct FDInfo {
    FDInfoWrapper fdinfo;
    explicit FDInfo(const std::string& drm_node);
};
