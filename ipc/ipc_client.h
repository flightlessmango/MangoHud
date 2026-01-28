#pragma once
#include <systemd/sd-bus.h>
#include <deque>
#include "imgui.h"
#include <atomic>
#include <thread>
#include <string>
#include <mutex>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "client.h"
class spdlogSink;
class IPCClient {
public:
    std::atomic<bool> needs_import{false};
    std::atomic<bool> connected{false};
    std::mutex m;
    uint64_t seq = 0;
    Fdinfo fdinfo;
    float fps_limit = 0;
    int64_t renderMinor = 0;
    std::string pEngineName;

    IPCClient();

    void start(int64_t renderMinor, std::string& pEngineName);

    bool init();
    void stop();

    void add_to_queue(uint64_t now) {
        std::unique_lock lock(samples_mtx);
        samples.push_back({seq, now});
        seq++;

        if (last_push == 0) {
            last_push = now;
            return;
        }

        if (now - last_push >= 7'000'000) {
            wake_up_fd(wake_fd);
            last_push = now;
        }
    }

    void drain_queue();
    int push_queue();
    int ready_frame();
    int send_release_fence(int fd);
    void queue_fence(int fd) {
        {
            std::lock_guard lock(fences_mtx);
            fences.push_back(std::move(fd));
        }
        wake_up_fd(wake_fd);
    }
    bool on_connect();
    void send_spdlog(const int level, const char* file, const int line, const std::string& text);

    ~IPCClient() {
        stop();
    }

private:
    static int on_dmabuf(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int on_fence(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int on_config(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    void bus_thread();
    static int on_server_owner_changed(sd_bus_message* m, void* userdata, sd_bus_error*);
    bool connect_bus();
    void disconnect_bus();
    bool run_bus();
    int request_fd_from_server();

    inline void wake_up_fd(int wake_fd) {
        const uint64_t one = 1;

        while (true) {
            const ssize_t n = write(wake_fd, &one, sizeof(one));
            if (n == (ssize_t)sizeof(one))
                return;
            if (n == -1 && errno == EINTR)
                continue;

            return;
        }
    }

    inline void drain_wake_fd(int wake_fd) {
        while (true) {
            uint64_t v = 0;
            const ssize_t n = read(wake_fd, &v, sizeof(v));
            if (n == (ssize_t)sizeof(v))
                continue;
            if (n == -1 && errno == EINTR)
                continue;

            if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                return;

            return;
        }
    }

    std::atomic<bool> quit{false};
    std::thread thread;
    std::deque<Sample> samples;
    std::mutex samples_mtx;
    std::deque<int> fences;
    std::mutex fences_mtx;

    std::mutex bus_mtx;
    sd_bus* bus = nullptr;
    sd_bus_slot* dmabuf_slot = nullptr;
    sd_bus_slot* fence_slot = nullptr;
    sd_bus_slot* config_slot = nullptr;
    int wake_fd;
    int socket_fd = -1;
    int pending_acquire_fd = -1;
    uint64_t last_push = 0;
    std::shared_ptr<spdlog::logger> logger;
};

extern std::shared_ptr<IPCClient> ipc;

class spdlogSink final : public spdlog::sinks::base_sink<std::mutex>  {
public:
    spdlogSink(IPCClient* dbus_) : dbus(dbus_),
               formatter(std::make_unique<spdlog::pattern_formatter>()){};

protected:
    IPCClient* dbus;
    std::unique_ptr<spdlog::formatter> formatter;

    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        std::string text(msg.payload.data(), msg.payload.size());

        const char* file = msg.source.filename ? msg.source.filename : "";
        int line = msg.source.line;
        int level = static_cast<int>(msg.level);

        dbus->send_spdlog(level, file, line, text);
    }

    void flush_() override {}
};
