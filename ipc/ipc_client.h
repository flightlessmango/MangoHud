#pragma once
#include <systemd/sd-bus.h>
#include "proto.h"
#include <deque>
#include "imgui.h"
#include <atomic>
#include <thread>
#include <string>
#include <mutex>
#include <spdlog/spdlog.h>

struct Fdinfo {
    uint64_t modifier = 0;
    uint32_t dmabuf_offset = 0;
    uint32_t stride = 0;
    uint32_t fourcc = 0;
    uint64_t plane_size = 0;

    uint32_t w = 0;
    uint32_t h = 0;
    int64_t server_render_minor = 0;

    int gbm_fd = -1;
    int opaque_fd = -1;
    uint64_t opaque_size = 0;
    uint64_t opaque_offset = 0;
};

class IPCClient {
public:
    std::atomic<bool> need_reconnect{false};
    std::atomic<bool> needs_import{false};
    std::mutex m;
    uint64_t seq = 0;
    Fdinfo fdinfo;
    float fps_limit = 0;

    // sending to server stuff
    int64_t renderMinor = 0;
    std::string pEngineName;

    IPCClient(int64_t renderMinor_, std::string pEngineName_) :
              renderMinor(renderMinor_), pEngineName(pEngineName_) {
        SPDLOG_DEBUG("init dbus client");
        thread = std::thread(&IPCClient::bus_thread, this);
    }

    bool init();
    void stop();

    void add_to_queue(uint64_t now) {
        {
            std::unique_lock lock(samples_mtx);
            samples.push_back({seq, now});
            seq++;
        }
    }

    void drain_queue();
    int push_queue();
    int ready_frame();
    int send_release_fence(int fd);
    void queue_fence(int fd) {
        std::lock_guard lock(fences_mtx);
        fences.push_back(std::move(fd));
    }
    ~IPCClient() {
        stop();
    }

private:
    static int on_dmabuf_ready(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int on_fence_ready(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int on_config(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    void bus_thread();
    bool handshake();
    static int on_server_owner_changed(sd_bus_message* m, void* userdata, sd_bus_error*);

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
    sd_bus_slot* server_watch = nullptr;
    int pending_acquire_fd = -1;
};
