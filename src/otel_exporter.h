#pragma once
// Simple OpenTelemetry-style metrics exporter for MangoHud
// This implementation avoids external dependencies and exposes a very small
// HTTP endpoint that returns a Prometheus-compatible text exposition which
// can be scraped by collectors and converted to OTLP if desired.
//
// The exporter is intentionally lightweight: it periodically samples the
// already collected MangoHud metric globals and serves the most recent
// values. It is independent of the HUD visibility or logging state.
//
// Configuration is driven through overlay_params (see overlay_params.h):
//  - otel (boolean) : Enable / disable exporter
//  - otel_listen (string host:port) : Listen interface and port (default 0.0.0.0:16969)
//  - otel_interval (uint, ms) : Internal sampling interval (default 1000)
//  - otel_startup_delay (uint, seconds) : Delay before starting server (default 0)
//
// All exported metrics include two constant labels:
//   pid   = current process id
//   exec  = executable (process name)
//
// Thread model:
//  - Collection thread wakes up every otel_interval ms and snapshots globals
//  - Server thread is a simple blocking accept loop; each connection gets
//    a copy of the last prepared metrics string and is closed immediately.
//
// This is a best-effort facility; failures to bind or accept are logged once
// and retried only on config changes.

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

struct overlay_params; // forward decl

class OTelExporter {
public:
    static OTelExporter& instance();

    // Called frequently from the render/update path; will start exporter
    // once enabled and startup delay elapsed. Safe to call many times.
    void maybe_start(const overlay_params* params, uint32_t vendorID);

    // Force a shutdown (e.g. on program exit) – idempotent.
    void stop();

    // For explicit config reload handling.
    void reconfigure(const overlay_params* params);

private:
    OTelExporter() = default;
    ~OTelExporter();
    OTelExporter(const OTelExporter&) = delete;
    OTelExporter& operator=(const OTelExporter&) = delete;

    void start_locked();
    void start_threads_unlocked();
    void collection_loop();
    void server_loop();
    void build_metrics_string();
    bool parse_listen(const std::string& listen, std::string& host, std::string& port);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_should_run{false};
    std::atomic<bool> m_server_ready{false};
    std::atomic<bool> m_rebuild{false};

    const overlay_params* m_params_ref = nullptr;
    std::mutex m_mutex;
    std::thread m_collect_thread;
    std::thread m_server_thread;

    std::chrono::steady_clock::time_point m_first_enable_call{};
    std::string m_listen_cached;
    uint32_t m_interval_cached = 1000; // ms
    uint32_t m_startup_delay_cached = 0; // seconds

    pid_t m_pid = 0;
    std::string m_exec_name;

    // Snapshot values
    struct Sample {
        double fps = 0.0;
        float frametime = 0.f; // ms
        float cpu_load = 0.f;
        float gpu_load = 0.f;
        float cpu_temp = 0.f;
        float gpu_temp = 0.f;
        float cpu_power = 0.f;
        float gpu_power = 0.f;
        double ram_used = 0.0; // MB
        double vram_used = 0.0; // MB
        uint64_t timestamp_ns = 0; // monotonic
    } m_latest;

    std::string m_metrics_text;
};

