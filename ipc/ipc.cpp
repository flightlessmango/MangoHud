#include <systemd/sd-bus.h>
#include <cstdint>
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
#include "../server/config.h"
#include "../server/server.h"

IPCServer::IPCServer(MangoHudServer* server_) : server(server_) {
    SPDLOG_DEBUG("init IPCServer");
    int r = sd_id128_randomize(&server_id);
    if (r < 0) {
        SPDLOG_ERROR("sd_id128_randomize {} ({})", r, strerror(-r));
        server_id = SD_ID128_NULL;
    }

    thread = std::thread(&IPCServer::dbus_thread, this);
}

IPCServer::~IPCServer() {
    stop.store(true);
    if (thread.joinable())
        thread.join();

    if (slot) sd_bus_slot_unref(slot);
    if (bus) sd_bus_unref(bus);
}

void IPCServer::prune_clients() {
    std::vector<std::shared_ptr<Client>> clients_;
    {
        std::lock_guard lock(clients_mtx);
        clients_ = clients;
    }

    int num_dead = 0;
    for (auto& client : clients_) {
        if (kill(client->pid, 0) != 0 || !client->active.load()) {
            client->active.store(false);
            num_dead++;
        }
    }

    if (num_dead == 0) return;

    std::lock_guard lock(clients_mtx);
    for (auto it = clients.begin(); it != clients.end(); ) {
        if (!(*it)->active.load()) {
            SPDLOG_DEBUG("Client disconnected {}", (*it)->pid);
            it = clients.erase(it);
        } else {
            ++it;
        }
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
        self->clients.push_back(std::make_shared<Client>(pid, self, self->server, client_bus));
    }

    r = sd_bus_reply_method_return(m, "h", client_fd);
    close(client_fd);
    return r;
}

void IPCServer::dbus_thread() {
    static const sd_bus_vtable vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("request_fd", "", "h", IPCServer::on_request_fd, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_VTABLE_END
    };

    int r = sd_bus_open_user(&bus);
    if (r < 0) {
        return;
    }

    r = sd_bus_request_name(bus, kBusName, 0);
    if (r < 0) {
        sd_bus_unref(bus);
        return;
    }

    r = sd_bus_add_object_vtable(bus, &slot, kObjPath, kIface, vtable, this);
    if (r < 0) {
        sd_bus_unref(bus);
        return;
    }

    while (!stop.load()) {
        r = sd_bus_process(bus, nullptr);
        if (r < 0) {
            break;
        }
        if (r > 0) {
            continue;
        }
        r = sd_bus_wait(bus, UINT64_MAX);
        if (r < 0) {
            break;
        }
    }
}
