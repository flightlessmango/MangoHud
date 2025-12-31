#pragma once
#include <systemd/sd-bus.h>
#include <cstdint>
#include <vector>
#include <drm/drm_fourcc.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <numeric>
#include <spdlog/spdlog.h>
#include "proto.h"
#include "mesa/os_time.h"
#include "../server/common/table_structs.h"
#include "../render/shared.h"

class MangoHudServer;
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
    int64_t renderMinor;
    clientRes resources;
    MangoHudServer* server;

    Client(std::string name, pid_t pid, int64_t renderMinor, MangoHudServer *server_, IPCServer* ipc_)
                 : pid(pid), frametimes(200, 0.0f), name(name),
                   renderMinor(renderMinor), resources(name), server(server_)
    {
        thread = std::thread(&Client::queue_frame, this);
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

    ~Client() {
        std::lock_guard lock(m);
        stop.store(true);
        if (thread.joinable())
            thread.join();
        if (resources.release_fd >= 0) ::close(resources.release_fd);
        resources.release_fd = -1;
    }

private:
    std::thread thread;
    std::atomic<bool> stop {false};
    bool ready_frame_blocking();
    void queue_frame();
};

static constexpr const char* kBusName = "io.mangohud.gbm";
static constexpr const char* kObjPath = "/io/mangohud/gbm";
static constexpr const char* kIface   = "io.mangohud.gbm1";
static constexpr uint32_t kProtoVersion = 1;

class IPCServer {
public:
    std::unordered_map<std::string, std::unique_ptr<Client>> clients;
    std::mutex clients_mtx;
    MangoHudServer *server;

    IPCServer(MangoHudServer *server_);

    void notify_frame_ready(std::deque<clientRes*> res) {
        std::lock_guard lock(q_mtx);

        for (auto* r : res) {
            int fd = -1;
            {
                std::lock_guard rlock(r->m);
                fd = r->acquire_fd;
                r->acquire_fd = -1;
            }

            if (fd >= 0)
                fences.emplace_back(r->client_id, fd);

            r->initial_fence = true;
        }
    }

    void queue_dmabuf(clientRes* res) {
        SPDLOG_DEBUG("enqueue dmabuf");
        res->send_dmabuf = false;
        std::lock_guard lock(q_mtx);
        dmabufs.push_back(res);
    }

    void queue_config(std::string name) {
        SPDLOG_DEBUG("enqueue config for {}", name);
        std::lock_guard lock(q_mtx);
        configs.push_back(name);
    }

    void queue_configs() {
        SPDLOG_DEBUG("enqueue configs for all clients");
        std::vector<std::string> names;
        for (auto& [name, client] : clients)
        names.push_back(name);

        std::lock_guard lock(q_mtx);
        for (auto& name : names)
            configs.push_back(name);
    }

    ~IPCServer();

private:
    sd_bus *bus = nullptr;
    sd_bus_slot *slot = nullptr;
    sd_bus_track *track = nullptr;
    sd_bus_slot *owner_change_slot = nullptr;
    std::thread thread;
    std::atomic<bool> stop {false};
    std::mutex q_mtx;
    std::deque<sd_bus_message*> msgs;
    std::deque<std::pair<std::string, int>> fences;
    std::deque<clientRes*> dmabufs;
    std::deque<std::string> dmabuf_q;
    std::mutex dmabuf_m;
    std::deque<std::string> configs;
    std::deque<std::string> destroy_clients_q;

    static int track_handler(sd_bus_track *t, void *userdata);
    void maybe_track_sender(sd_bus_message *m);
    static int on_name_owner_changed(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int handshake(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int frame_samples(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int release_fence(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static inline const sd_bus_vtable ipc_vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("Handshake", "sx", "u", IPCServer::handshake, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("FrameSamples", "a(tt)", "", IPCServer::frame_samples, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("ReleaseFence", "h", "", IPCServer::release_fence, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_SIGNAL("DmabufReady", "tuuutuuxhhtt", 0),
        SD_BUS_VTABLE_END
    };
    int init();
    void dbus_thread();
    int emit_dmabuf_to(std::string& name);
    int emit_fence_to(std::string& name, int fd);
    void add_dmabuf_to_queue(std::string& name);
    static pid_t sender_pid(sd_bus_message* m);
    Client* get_client(sd_bus_message* m);
    void prune_clients();
    bool send_fence(std::string& name, int fd);
    void send_fences();
    bool send_dmabuf(clientRes& r);
    void send_dmabufs();
    bool send_config(std::string& name);
    void send_configs();

    Client* find_client_by_sender(std::string sender) {
        auto it = clients.find(sender);
        if (it == clients.end()) return nullptr;
        return it->second.get();
    }
};
