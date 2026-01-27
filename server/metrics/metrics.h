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

struct configSig {
    bool exists = false;
    std::int64_t size = 0;
    std::int64_t sec  = 0;
    std::int64_t nsec = 0;
};
using MetricValue = std::variant<int, float, std::vector<float>, std::string>;
struct Metric {
    std::optional<MetricValue> val;
    std::string unit;
};
using MetricTable = std::unordered_map<std::string,
                   std::unordered_map<std::string, Metric>>;
class Metrics {
public:
    std::shared_ptr<hudTable> table;
    Metrics(IPCServer& ipc);
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
    std::string configPath = "/home/crz/.config/MangoHud/MangoHud.yml";
    configSig previousSig {};
    MetricTable metrics;
    MetricTable client_metrics;

    bool read_sig(const char* path, configSig& out);
    bool sig_changed(const configSig& a, const configSig& b);
    bool reload_config();

    std::shared_ptr<hudTable> assign_values(hudTable* t, pid_t pid);
    void format_into(std::string& dst, const char* fmt, ...) const;
    std::string engine_name(const std::string& engine) ;
};
