#pragma once
#include <systemd/sd-bus.h>
#include <deque>
#include "imgui.h"
#include <atomic>
#include <thread>
#include <string>
#include <mutex>
#include <spdlog/spdlog.h>
#include "spdlog_forward.h"
#include "client.h"

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
        wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        thread = std::thread(&IPCClient::bus_thread, this);
    }

    bool init();
    void stop();

    void add_to_queue(uint64_t now) {
        std::unique_lock lock(samples_mtx);
        samples.push_back({seq, now});
        seq++;
        uint64_t one = 1;
        (void)write(wake_fd, &one, sizeof(one));
    }

    void drain_queue();
    int push_queue();
    int ready_frame();
    int send_release_fence(int fd);
    void queue_fence(int fd) {
        std::lock_guard lock(fences_mtx);
        fences.push_back(std::move(fd));
        uint64_t one = 1;
        (void)write(wake_fd, &one, sizeof(one));
    }
    bool on_connect();
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
};
