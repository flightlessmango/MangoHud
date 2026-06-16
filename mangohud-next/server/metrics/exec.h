#pragma once

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <sys/types.h>

#include <spdlog/spdlog.h>

#include "../common/helpers.hpp"

class Exec {
public:
    Exec();
    ~Exec();

    std::pair<bool, std::string> get(const std::string& command);

private:
    struct Job {
        pid_t pid = -1;
        unique_fd stdout_fd;
        unique_fd stderr_fd;
        std::string stdout_output;
        std::string stderr_output;
        std::chrono::steady_clock::time_point started;

        Job(pid_t pid_, unique_fd stdout_fd_, unique_fd stderr_fd_,
            std::chrono::steady_clock::time_point started_);
        ~Job();
    };

    struct Entry {
        std::mutex m;
        std::string cached_output;
        std::string last_stderr_log;
        std::string last_spawn_error;
        std::chrono::steady_clock::time_point last_request;
        std::unique_ptr<Job> job;
        bool valid = false;

        bool running() const { return job != nullptr; }
    };

    using EntryList = std::vector<std::pair<std::string, std::shared_ptr<Entry>>>;

    std::thread thread;
    std::atomic<bool> stop {false};
    std::mutex m;
    std::unordered_map<std::string, std::shared_ptr<Entry>> entries;

    std::chrono::milliseconds interval{1000};
    std::chrono::milliseconds timeout{750};
    std::chrono::milliseconds stale_after{3000};
    static constexpr size_t stdout_limit = 4096;
    static constexpr size_t stderr_limit = 2048;

    void run();
    void tick();
    void remove_stale_entries(std::chrono::steady_clock::time_point now);
    void update_running_jobs(EntryList& active_entries,
                             std::chrono::steady_clock::time_point now);
    void drain_job(Job& job);
    void drain_fd(unique_fd& fd, std::string& output, size_t limit);
    void finish_job(const std::string& command, Entry* entry, std::unique_ptr<Job> job,
                    int status, bool timed_out);
    bool spawn(const std::string& command, Entry* entry);
    void log_spawn_error(const std::string& command, Entry& entry, const char* error);
};
