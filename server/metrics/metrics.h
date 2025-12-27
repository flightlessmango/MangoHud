#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include "../config.h"
#include "gpu/gpu.hpp"
#include "cpu/cpu.hpp"
#include "../../ipc/ipc.h"

struct configSig {
    bool exists = false;
    std::int64_t size = 0;
    std::int64_t sec  = 0;
    std::int64_t nsec = 0;
};

using MetricValue = std::variant<float, std::vector<float>>;
using MetricTable = std::unordered_map<std::string,
                   std::unordered_map<std::string, MetricValue>>;
class Metrics {
public:
    std::unique_ptr<HudTable> table;
    Metrics(IPCServer& ipc);
    void update();
    std::optional<MetricValue> get(const char* a, const char* b,
                                   const std::string& name = {});
    void update_table();
    void populate_tables();
    void update_client();

    ~Metrics() {
        stop.store(true);
        if (thread.joinable())
            thread.join();
    }

private:
    IPCServer& ipc;
    GPUS gpus;
    CPU cpu;
    std::mutex m;
    std::thread thread;
    std::thread table_thread;
    std::thread client_thread;
    std::thread populate_tables_t;
    std::atomic<bool> stop {false};
    ColorCache color;
    const char* configPath = "/home/crz/.config/MangoHud/MangoHud.yml";
    configSig previousSig {};
    MetricTable metrics;
    MetricTable client_metrics;

    bool read_sig(const char* path, configSig& out);
    bool sig_changed(const configSig& a, const configSig& b);
    bool reload_config();

    HudTable assign_values(HudTable& t, pid_t& pid, std::string client_name);
    char* format_string(const char *fmt, ...);
};
