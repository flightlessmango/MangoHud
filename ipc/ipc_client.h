#pragma once
#include <systemd/sd-bus.h>
#include <deque>
#include "imgui.h"
#include <atomic>
#include <thread>
#include <string>
#include <mutex>
#include <future>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "client.h"

class spdlogSink;
class Layer;
class IPCClient {
public:
    std::atomic<bool> needs_import{false};
    std::atomic<bool> connected{false};
    std::mutex m;
    Fdinfo fdinfo;
    float fps_limit = 0;
    int64_t renderMinor = 0;
    std::string pEngineName;
    int buffer_size = 0;

    IPCClient(Layer* layer_ = nullptr);

    void start(int64_t renderMinor, std::string& pEngineName, int image_count, std::shared_ptr<IPCClient> shared);

    bool init();
    void stop();

    void add_to_queue(uint64_t now) {
        {
            static uint64_t seq;
            std::unique_lock lock(samples_mtx);
            samples.push_back({seq, now});
            seq++;
        }

        if (last_push == 0) {
            last_push = now;
            return;
        }

        if (now - last_push >= 4'000'000) {
            push_queue();
            // wake_up_fd(wake_fd);
            last_push = now;
        }
    }

    void drain_queue();
    int push_queue();
    bool on_connect();
    void send_spdlog(const int level, const char* file, const int line, const std::string& text);
    void send_semaphores(std::vector<int> sema);
    void frame_ready(uint32_t idx, int fd);
    int next_frame() {
        std::lock_guard lock(sync_mtx);
        if (frame_queue.empty())
            return -1;

        ready_frame& frame = frame_queue.front();
        if (sync_fd_signaled(frame.fd.get())) {
            frame_queue.pop_front();
            return frame.idx;
        }

        return -1;
    }

    ~IPCClient() {
        stop();
    }

private:
    Layer* layer;

    std::atomic<bool> quit{false};
    std::thread thread;
    std::deque<Sample> samples;
    std::mutex samples_mtx;
    std::mutex sync_mtx;
    std::atomic<bool> stop_wait {false};
    std::thread wait_thread;

    std::mutex bus_mtx;
    sd_bus* bus = nullptr;
    sd_bus_slot* dmabuf_slot = nullptr;
    sd_bus_slot* fence_slot = nullptr;
    sd_bus_slot* config_slot = nullptr;
    sd_bus_slot* frame_slot = nullptr;
    int wake_fd = -1;
    int socket_fd = -1;
    uint64_t last_push = 0;
    std::shared_ptr<spdlog::logger> logger;
    std::deque<ready_frame> frame_queue;
    int work_eventfd = -1;
    std::mutex work_mtx;
    std::queue<std::packaged_task<void()>> work_q;
    sd_event* event = nullptr;
    sd_event_source* work_src = nullptr;

    static int on_dmabuf(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int on_config(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int on_frame(sd_bus_message* m, void* userdata, sd_bus_error*);
    void bus_thread();
    static int on_server_owner_changed(sd_bus_message* m, void* userdata, sd_bus_error*);
    bool connect_bus();
    void disconnect_bus();
    void run_bus();
    int request_fd_from_server();
    static int on_work_event(sd_event_source *s, int fd, uint32_t revents, void *userdata);
    template <class F>
    void post(F&& fn) {
        {
            std::lock_guard<std::mutex> lock(work_mtx);
            work_q.emplace(std::forward<F>(fn));
        }

        uint64_t one = 1;
        ssize_t n = write(work_eventfd, &one, sizeof(one));
        (void)n;
    }
    void sync_wait();
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
