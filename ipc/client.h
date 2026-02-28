#pragma once
#include <mutex>
#include <deque>
#include <string>
#include <memory>
#include <thread>
#include <functional>
#include <queue>
#include <memory>
#include <future>
#include <systemd/sd-bus.h>
#include "../render/shared.h"
#include <poll.h>
#include <sys/eventfd.h>

constexpr size_t   FT_MAX = 200;
constexpr uint64_t KEEP_NS = 500000000ULL;

struct Sample {
    uint64_t seq;
    uint64_t t_ns;
};

struct Fdinfo {
    uint64_t modifier = 0;
    uint32_t dmabuf_offset = 0;
    uint32_t stride = 0;
    uint32_t fourcc = 0;
    uint64_t plane_size = 0;

    uint32_t w = 0;
    uint32_t h = 0;
    int64_t server_render_minor = 0;

    bool has_gbm;
    uint64_t opaque_size = 0;
    uint64_t opaque_offset = 0;

    std::vector<unique_fd> dmabuf_buffer;
    std::vector<unique_fd> opaque_buffer;
    std::vector<unique_fd> semaphores;
};

class IPCServer;
class MangoHudServer;
class Client {
public:
    pid_t pid;
    std::mutex m;
    std::condition_variable cv;
    std::shared_ptr<hudTable> table;
    std::deque<Sample> samples;
    std::mutex samples_m;
    std::deque<float> frametimes;
    std::string name;
    uint64_t n_frames = 0;
    uint64_t last_fps_update = 0;
    uint64_t seq_last = 0, t_last = 0;
    uint64_t dropped = 0;
    double previous_fps = 0;
    bool have_prev = false;
    std::string pEngineName;
    int64_t renderMinor = 0;
    std::shared_ptr<clientRes> resources;
    IPCServer* ipc;
    MangoHudServer* server;
    sd_bus* bus;
    sd_bus_slot* slot;
    std::atomic<bool> active {true};
    std::deque<ready_frame> frame_queue;
    std::atomic<bool> stop {false};

    Client(pid_t pid_, IPCServer* ipc_, MangoHudServer* server_, sd_bus* bus_)
           : pid(pid_), frametimes(200, 0.0f), resources(std::make_shared<clientRes>()),
           ipc(ipc_), server(server_), bus(bus_) {}

    float avg_fps_from_samples() {
        std::lock_guard lock(samples_m);
        if (samples.size() < 2) return previous_fps;

        const auto& b = samples.back();

        if (last_fps_update != 0 &&
            (b.t_ns - last_fps_update) < 500000000ULL) {
            return previous_fps;
        }

        const auto& a = samples.front();
        uint64_t dseq = b.seq - a.seq;
        uint64_t dt   = b.t_ns - a.t_ns;
        if (dseq == 0 || dt == 0) return previous_fps;

        previous_fps = (float)(1e9 * (double)dseq / (double)dt);
        last_fps_update = b.t_ns;
        return previous_fps;
    }

    void init(std::shared_ptr<Client>& shared);
    void send_dmabuf();
    void send_config();
    static int on_connect(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    void set_dead();
    void frame_ready(uint32_t idx);
    void stop_and_join();

    ~Client();

private:
    std::thread thread;
    sd_bus_slot* handshake_slot;
    sd_bus_slot* frame_samples_slot;
    sd_bus_slot* spdlog_slot;
    sd_bus_slot* frame_slot;
    std::mutex frame_m;
    std::thread run_t;
    sd_event* event = nullptr;
    sd_event_source* stop_src = nullptr;
    sd_event_source* work_src = nullptr;
    int stop_eventfd = -1;
    int work_eventfd = -1;
    std::mutex work_mtx;
    std::queue<std::packaged_task<void()>> work_q;
    std::shared_ptr<spdlog::logger> logger;
    int buffer_size;
    std::weak_ptr<Client> self_weak;

    static inline const sd_bus_vtable vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_VTABLE_END
    };

    bool ready_frame_blocking();
    void queue_frame();
    void dbus_thread();
    void run();
    int try_acquire_buffer();
    void setup_handshake(std::string member, sd_bus_slot** slot,
                         sd_bus_message_handler_t callback, std::shared_ptr<Client>& shared);

    static int frame_samples(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int spdlog_msg(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int on_frame(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int on_bus_disconnected(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    static int on_stop_event(sd_event_source *s, int fd, uint32_t revents, void *userdata);
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
    void wait_on_fences();
};
