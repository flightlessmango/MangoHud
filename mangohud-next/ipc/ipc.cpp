#include <cstdint>
#include <algorithm>
#include <vector>
#include <drm/drm_fourcc.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <sys/socket.h>
#include "ipc.h"
#include "ipc_client.h"
#include "ipc_abi_hash.h"
#include "../server/config.h"
#include "../server/server.h"
#include "../server/common/helpers.hpp"

IPCServer::IPCServer(MangoHudServer* server_) : server(server_) {
    SPDLOG_DEBUG("init IPCServer");
    int r = sd_id128_randomize(&server_id);
    if (r < 0) {
        SPDLOG_ERROR("sd_id128_randomize {} ({})", r, strerror(-r));
        server_id = SD_ID128_NULL;
    }
}

void IPCServer::record_server_request() {
    std::lock_guard lock(clients_mtx);
    server_request_time = os_time_get_nano();
}

void IPCServer::start() {
    idle_since = os_time_get_nano();
    thread = std::thread(&IPCServer::dbus_thread, this);
}

bool IPCServer::should_stop() {
    if (stop.load())
        return true;

    prune_clients();
    std::lock_guard lock(clients_mtx);
    const auto now = os_time_get_nano();
    if (!clients.empty()) {
        idle_since = now;
        return false;
    }
    if (now - std::max(idle_since, server_request_time) < 5'000'000'000LL)
        return false;

    SPDLOG_DEBUG("No clients remain; stopping server");
    return true;
}

int IPCServer::on_preflight(sd_bus_message* message, void* userdata, sd_bus_error*)
{
    auto* self = static_cast<IPCServer*>(userdata);
    self->record_server_request();
    const char* name = nullptr;
    int result = sd_bus_message_read(message, "s", &name);
    if (result <= 0)
        return result < 0 ? result : -EINVAL;

    bool excluded;
    {
        std::lock_guard lock(self->server->config->m);
        excluded = self->server->config->is_blacklisted(name);
    }
    return sd_bus_reply_method_return(message, "bt", static_cast<int>(excluded), uint64_t(MANGOHUD_IPC_ABI_HASH));
}

std::shared_ptr<Client> IPCServer::focused_client(std::string_view display)
{
    const auto candidates = snapshot(clients, clients_mtx);

    for (const auto& client : candidates) {
        std::lock_guard lock(client->m);
        if (client->active.load() &&
            client->wayland_display == display && client->focused())
            return client;
    }
    return nullptr;
}

IPCServer::~IPCServer() {
    stop.store(true);
    if (thread.joinable())
        thread.join();

    std::vector<std::shared_ptr<Client>> clients_;
    {
        std::lock_guard lock(clients_mtx);
        clients_.swap(clients);
    }

    for (auto& client : clients_)
        client->stop_and_join();
    clients_.clear();

    if (slot) sd_bus_slot_unref(slot);
    if (bus) sd_bus_unref(bus);
}

void IPCServer::prune_clients() {
    const auto candidates = snapshot(clients, clients_mtx);

    for (const auto& client : candidates) {
        if (kill(client->pid, 0) != 0)
            client->active.store(false);
    }

    std::vector<std::shared_ptr<Client>> retired;
    {
        std::lock_guard lock(clients_mtx);
        std::erase_if(clients, [&](const auto& client) {
            if (client->active.load())
                return false;
            retired.push_back(client);
            return true;
        });
    }

    for (const auto& client : retired) {
        SPDLOG_DEBUG("Client disconnected {}", client->pid);
        client->stop_and_join();
    }
}

int IPCServer::on_request_fd(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *self = static_cast<IPCServer *>(userdata);

    sd_bus_creds *creds = nullptr;
    int r = sd_bus_query_sender_creds(m, SD_BUS_CREDS_PID, &creds);
    if (r < 0) {
        return r;
    }

    pid_t pid = 0;
    (void)sd_bus_creds_get_pid(creds, &pid);
    sd_bus_creds_unref(creds);

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
        return -errno;
    }

    int server_fd = sv[0];
    int client_fd = sv[1];

    sd_bus *client_bus = nullptr;
    r = sd_bus_new(&client_bus);
    if (r < 0) {
        close(server_fd);
        close(client_fd);
        return r;
    }

    int server_fd2 = dup(server_fd);
    if (server_fd2 < 0) {
        sd_bus_unref(client_bus);
        close(server_fd);
        close(client_fd);
        return -errno;
    }

    r = sd_bus_set_fd(client_bus, server_fd, server_fd2);
    if (r < 0) {
        sd_bus_unref(client_bus);
        close(server_fd);
        close(server_fd2);
        close(client_fd);
        return r;
    }

    r = sd_bus_set_server(client_bus, 1, self->server_id);
    if (r < 0) {
        sd_bus_unref(client_bus);
        close(client_fd);
        close(server_fd);
        return r;
    }

    r = sd_bus_start(client_bus);
    if (r < 0) {
        sd_bus_unref(client_bus);
        close(client_fd);
        return r;
    }

    {
        self->prune_clients();
        std::lock_guard lock(self->clients_mtx);
        auto client = std::make_shared<Client>(pid, self, self->server, client_bus);
        client->init(client);
        self->clients.push_back(client);
        SPDLOG_DEBUG("Client connected {}", client->pid);
    }

    r = sd_bus_reply_method_return(m, "h", client_fd);
    close(client_fd);
    return r;
}

int IPCServer::on_get_clients(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *self = static_cast<IPCServer *>(userdata);
    self->record_server_request();

    auto json = self->server->metrics->clients_json_snapshot();
    return sd_bus_reply_method_return(m, "s", json.c_str());
}

int IPCServer::on_get_system(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *self = static_cast<IPCServer *>(userdata);
    self->record_server_request();

    auto json = self->server->metrics->system_json_snapshot();
    return sd_bus_reply_method_return(m, "s", json.c_str());
}

void IPCServer::dbus_thread() {
    static const sd_bus_vtable vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("request_fd", "", "h", IPCServer::on_request_fd, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("preflight", "s", "bt", IPCServer::on_preflight, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("get_clients", "", "s", IPCServer::on_get_clients, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("get_system", "", "s", IPCServer::on_get_system, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END
    };

    int r = sd_bus_open_user(&bus);
    if (r >= 0)
        r = sd_bus_add_object_vtable(bus, &slot, kObjPath, kIface, vtable, this);
    if (r >= 0)
        r = sd_bus_request_name(bus, kBusName, 0);
    if (r < 0) {
        SPDLOG_ERROR("server bus setup failed: {} ({})", r, strerror(-r));
        stop.store(true);
        return;
    }

    while (!stop.load()) {
        r = sd_bus_process(bus, nullptr);
        if (r < 0)
            break;

        if (r > 0)
            continue;
        r = sd_bus_wait(bus, 1000000);
        if (r < 0)
            break;
    }
    bus = sd_bus_flush_close_unref(bus);
    stop.store(true);
}
