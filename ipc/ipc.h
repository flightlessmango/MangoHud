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
#include "mesa/os_time.h"
#include "../server/common/table_structs.h"
#include "../render/shared.h"
#include "client.h"

class MangoHudServer;
class IPCServer {
public:
    std::unordered_map<pid_t, std::unique_ptr<Client>> clients;
    std::mutex clients_mtx;
    std::set<int> dead_clients;
    std::mutex dead_clients_mtx;
    MangoHudServer *server;

    IPCServer(MangoHudServer *server_);

    void prune_clients();
    ~IPCServer();

private:
    sd_bus *bus = nullptr;
    sd_bus_slot *slot = nullptr;
    sd_id128_t server_id;
    std::thread thread;
    std::atomic<bool> stop {false};
    std::mutex q_mtx;
    std::deque<sd_bus_message*> msgs;
    std::deque<std::pair<std::string, int>> fences;
    std::deque<clientRes*> dmabufs;
    std::deque<std::string> dmabuf_q;
    std::mutex dmabuf_m;
    std::deque<std::string> configs;

    int init();
    void socket_thread();
    int make_socket(const std::string &path);
    void dbus_thread();
    static int on_request_fd(sd_bus_message *m, void *userdata, sd_bus_error*);
};
