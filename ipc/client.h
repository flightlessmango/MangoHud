#pragma once
#include <mutex>
#include <deque>
#include <string>
#include <memory>
#include <thread>
#include <functional>
#include <queue>
#include <memory>
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
    int gbm_fd = -1;
    int opaque_fd = -1;
    uint64_t opaque_size = 0;
    uint64_t opaque_offset = 0;
};

class IPCServer;
class MangoHudServer;
class Client {
public:
    pid_t pid;
    mutable std::mutex m;
    mutable std::shared_ptr<hudTable> table;
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

    Client(pid_t pid_, IPCServer* ipc_, MangoHudServer* server_, sd_bus* bus_)
           : pid(pid_), frametimes(200, 0.0f), resources(std::make_shared<clientRes>()),
           ipc(ipc_), server(server_), bus(bus_)
    {
        std::string match_handshake =
            "type='signal',"
            "path='" + std::string(kObjPath) + "',"
            "interface='" + std::string(kIface) + "',"
            "member='on_connect'";

        int r = sd_bus_add_match(bus, &handshake_slot, match_handshake.c_str(), &Client::on_connect, this);
        if (r < 0) {
            SPDLOG_ERROR("match_handshake {} ({}) ObjPath: {} Iface: {}", r, strerror(-r), kObjPath, kIface);
            set_dead();
        }

        std::string match_release_fence =
            "type='signal',"
            "path='" + std::string(kObjPath) + "',"
            "interface='" + std::string(kIface) + "',"
            "member='release_fence'";

        r = sd_bus_add_match(bus, &release_fence_slot, match_release_fence.c_str(), &Client::release_fence, this);
        if (r < 0) {
            SPDLOG_ERROR("match_release_fence {} ({}) ObjPath: {} Iface: {}", r, strerror(-r), kObjPath, kIface);
            set_dead();
        }

        std::string match_frame_samples =
            "type='signal',"
            "path='" + std::string(kObjPath) + "',"
            "interface='" + std::string(kIface) + "',"
            "member='frame_samples'";

        r = sd_bus_add_match(bus, &frame_samples_slot, match_frame_samples.c_str(), &Client::frame_samples, this);
        if (r < 0) {
            SPDLOG_ERROR("match_release_fence {} ({}) ObjPath: {} Iface: {}", r, strerror(-r), kObjPath, kIface);
            set_dead();
        }

        std::string match_spdlog =
            "type='signal',"
            "path='" + std::string(kObjPath) + "',"
            "interface='" + std::string(kIface) + "',"
            "member='spdlog'";

        r = sd_bus_add_match(bus, &spdlog_slot, match_spdlog.c_str(), &Client::spdlog_msg, this);
        if (r < 0) {
            SPDLOG_ERROR("match_spdlog {} ({}) ObjPath: {} Iface: {}", r, strerror(-r), kObjPath, kIface);
            set_dead();
        }

        thread = std::thread(&Client::dbus_thread, this);
        run_t =  std::thread(&Client::run, this);
    }

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

    bool ready_frame();
    void send_dmabuf();
    void send_config();
    void send_fence();
    static int on_connect(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    void set_dead();

    ~Client();

private:
    std::shared_ptr<VkCtx> vk;
    std::thread thread;
    std::atomic<bool> stop {false};
    sd_bus_slot* handshake_slot;
    sd_bus_slot* release_fence_slot;
    sd_bus_slot* frame_samples_slot;
    sd_bus_slot* spdlog_slot;
    std::thread run_t;
    sd_event* event = nullptr;
    sd_event_source* stop_src = nullptr;
    sd_event_source* work_src = nullptr;
    int stop_eventfd = -1;
    int work_eventfd = -1;
    std::mutex work_mtx;
    std::queue<std::function<void()>> work_q;
    std::shared_ptr<spdlog::logger> logger;

    static inline const sd_bus_vtable vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_VTABLE_END
    };

    bool ready_frame_blocking();
    void queue_frame();
    void dbus_thread();
    void run();

    static int release_fence(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int frame_samples(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int spdlog_msg(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int on_bus_disconnected(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    static int on_stop_event(sd_event_source *s, int fd, uint32_t revents, void *userdata);
    static int on_work_event(sd_event_source *s, int fd, uint32_t revents, void *userdata);
    void post(std::function<void()> fn);
};
