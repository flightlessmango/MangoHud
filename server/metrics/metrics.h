#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <string_view>
#include "../config.h"
#include "gpu/gpu.hpp"
#include "cpu/cpu.hpp"
#include "../../ipc/ipc.h"
#include "../../render/colors.h"

using MetricValue = std::variant<int, float, std::vector<float>, std::string>;
struct Metric {
    std::optional<MetricValue> val;
    std::string unit;
};
using MetricTable = std::unordered_map<std::string,
                   std::unordered_map<std::string, Metric>>;
class Metrics {
public:
    std::shared_ptr<Config> cfg;
    Metrics(IPCServer& ipc, std::shared_ptr<Config> cfg_);
    void update();
    Metric get(const char* a, const char* b, const pid_t pid);
    void update_table();
    void populate_tables();
    void update_client();

    ~Metrics() {
        stop.store(true);
        if (thread.joinable())
            thread.join();

        if (client_thread.joinable())
            client_thread.join();
    }

private:
    IPCServer& ipc;
    GPUS gpus;
    CPU cpu;
    std::mutex m;
    std::thread thread;
    std::thread client_thread;
    std::atomic<bool> stop {false};
    ColorCache color;
    MetricTable metrics;
    MetricTable client_metrics;

    void assign_values(hudTable* t, pid_t pid, hudTable* render_table);
    void format_into(std::string& dst, const char* fmt, ...) const;
    std::string engine_name(const std::string& engine) ;
};
