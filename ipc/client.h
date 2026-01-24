#pragma once
#include <mutex>
#include <deque>
#include <string>
#include <memory>
#include <thread>
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
class Client {
public:
    pid_t pid;
    mutable std::mutex m;
    mutable std::shared_ptr<hudTable> table;
    std::deque<Sample> samples;
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
    clientRes resources;
    IPCServer* ipc;
    sd_bus* bus;
    sd_bus_slot* slot;
    bool active = true;

    Client(pid_t pid_, IPCServer* ipc_, sd_bus* bus_) : pid(pid_),
           frametimes(200, 0.0f), ipc(ipc_), bus(bus_)
    {
        // int r = sd_bus_add_object_vtable(bus, &slot, kObjPath, kIface, vtable, this);
        // if (r < 0) {
        //     SPDLOG_ERROR("sd_bus_add_object_vtable failed {} ({}) pid={}", r, strerror(-r), (int)pid);
        //     active = false;
        // }

        stop_eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

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

        thread = std::thread(&Client::dbus_thread, this);
        dead_t = std::thread(&Client::check_dead, this);
    }

    float avg_fps_from_samples() {
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
    bool send_dmabuf();
    bool send_config();
    bool send_fence();
    static int on_connect(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    void set_dead();

    ~Client() {
        stop.store(true);
        if (stop_eventfd >= 0) {
            uint64_t one = 1;
            for (;;) {
                ssize_t n = write(stop_eventfd, &one, sizeof(one));
                if (n == (ssize_t)sizeof(one)) break;
                if (n < 0 && errno == EINTR) continue;
                break;
            }
        }

        sd_event *e = event;
        if (e) {
            sd_event_exit(e, 0);
        }

        if (thread.joinable())
            thread.join();

        if (dead_t.joinable())
            dead_t.join();

        if (bus)
            sd_bus_unref(bus);

        if (resources.release_fd >= 0) ::close(resources.release_fd);

    }

private:
    std::thread thread;
    std::atomic<bool> stop {false};
    sd_bus_slot* handshake_slot;
    sd_bus_slot* release_fence_slot;
    sd_bus_slot* frame_samples_slot;
    int stop_eventfd;
    std::thread dead_t;
    sd_event *event = nullptr;

    static inline const sd_bus_vtable vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_VTABLE_END
    };

    bool ready_frame_blocking();
    void queue_frame();
    void dbus_thread();
    static int release_fence(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int frame_samples(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int on_bus_disconnected(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    static int on_stop_event(sd_event_source *s, int fd, uint32_t revents, void *userdata);

    void check_dead() {
        while (!stop.load()) {
            if (kill(pid, 0) != 0)
                set_dead();

            sleep(0.1);
        }
    }
};
