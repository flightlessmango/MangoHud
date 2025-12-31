#include <systemd/sd-bus.h>
#include <cstdint>
#include <vector>
#include <drm/drm_fourcc.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include "ipc.h"
#include "ipc_client.h"
#include "../server/config.h"
#include "../server/server.h"

clientRes::~clientRes() {
    destroy_client_res(device, *this);
}

void clientRes::reset() {
    destroy_client_res(device, *this);
    send_dmabuf = false;
    reinit_dmabuf = false;
    initialized = false;
}

bool Client::ready_frame() {
    std::lock_guard lock(resources.m);
    if (resources.release_fd < 0 && !resources.initial_fence)
        return true;

    if (!sync_fd_signaled(resources.release_fd)) return false;

    ::close(resources.release_fd);
    resources.release_fd = -1;
    return true;
}

bool Client::ready_frame_blocking() {
    std::lock_guard lock(resources.m);
    if (resources.release_fd < 0 && !resources.initial_fence)
        return true;

    if (!synd_fd_blocking(resources.release_fd)) return false;

    ::close(resources.release_fd);
    resources.release_fd = -1;
    return true;
}

void Client::queue_frame() {
    // TODO revisit this, let clients queue their own frames
    // while (!stop.load()) {
    //     if (ready_frame_blocking()) {
    //         auto& r = resources;
    //         if (r.reinit_dmabuf)
    //             r.reset();

    //         server->queue_frame(r, renderMinor);
    //         if (r.send_dmabuf)
    //             ipc->queue_dmabuf(&r);
    //     }
    // }
}

IPCServer::IPCServer(MangoHudServer* server_) : server(server_) {
    SPDLOG_DEBUG("init IPCServer");
    thread = std::thread(&IPCServer::dbus_thread, this);
}

IPCServer::~IPCServer() {
    stop.store(true);
    if (thread.joinable())
        thread.join();

    if (track) sd_bus_track_unref(track);
    if (slot) sd_bus_slot_unref(slot);
    if (bus) sd_bus_unref(bus);
}

Client* IPCServer::get_client(sd_bus_message* m) {
    const char* sender = sd_bus_message_get_sender(m);
    if (!sender) return nullptr;
    std::lock_guard<std::mutex> lock(clients_mtx);
    return find_client_by_sender(sender);
}

void IPCServer::maybe_track_sender(sd_bus_message *m) {
    const char *sender = sd_bus_message_get_sender(m);
    if (!sender) return;
    const char* EngineName = "";
    int64_t renderMinor;
    (void) sd_bus_message_read(m, "sx", &EngineName, &renderMinor);
    bool inserted = false;
    {
        std::lock_guard<std::mutex> lock(clients_mtx);
        auto client = std::make_unique<Client>(sender, sender_pid(m), renderMinor, server, this);
        client->pEngineName = EngineName;
        inserted = clients.try_emplace(sender, std::move(client)).second;
    }
    if (!inserted) return;
    queue_config(sender);
    SPDLOG_DEBUG("Client connected {}", sender);
}

int IPCServer::on_name_owner_changed(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<IPCServer*>(userdata);

    const char *name, *old_owner, *new_owner;
    int r = sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner);
    if (r < 0) return 0;

    if (!name || name[0] != ':') return 0;

    if (new_owner && new_owner[0] == '\0') {
        std::lock_guard<std::mutex> lock(self->clients_mtx);
        self->destroy_clients_q.push_back(name);
    }

    return 0;
}

bool IPCServer::send_fence(std::string& name, int fd) {
    sd_bus_message* msg = nullptr;

    int r = sd_bus_message_new_signal(bus, &msg, kObjPath, kIface, "FenceReady");

    if (r < 0) {
        SPDLOG_DEBUG("failed to set signal {}", r);
        close(fd);
        return false;
    }

    r = sd_bus_message_set_destination(msg, name.c_str());
    if (r < 0) {
        SPDLOG_DEBUG("failed to set destination {}", r);
        sd_bus_message_unref(msg);
        close(fd);
        return false;
    }

    r = sd_bus_message_append(msg, "h", fd);
    close(fd);
    if (r < 0) {
        SPDLOG_DEBUG("failed to append to message {}", r);
        sd_bus_message_unref(msg);
        return false;
    }

    r = sd_bus_send(bus, msg, nullptr);
    sd_bus_message_unref(msg);
    if (r < 0) {
        SPDLOG_DEBUG("failed to send message {}", r);
        return false;
    }

    return true;
}

bool IPCServer::send_dmabuf(clientRes& r){
    SPDLOG_DEBUG("start send dmabuf");
    Fdinfo fdinfo;
    {
        std::lock_guard lock_c(r.m);
        r.send_dmabuf = false;
        fdinfo.modifier             = r.gbm.modifier;
        fdinfo.dmabuf_offset        = r.gbm.offset;
        fdinfo.stride               = r.gbm.stride;
        fdinfo.fourcc               = r.gbm.fourcc;
        fdinfo.plane_size           = r.gbm.plane_size;
        fdinfo.w                    = r.w;
        fdinfo.h                    = r.h;
        fdinfo.server_render_minor  = r.server_render_minor;
        fdinfo.gbm_fd               = r.gbm.fd;
        fdinfo.opaque_fd            = r.opaque_fd;
        fdinfo.opaque_size          = r.opaque_size;
        fdinfo.opaque_offset        = r.opaque_offset;
    }

    sd_bus_message* msg = nullptr;
    int ret = sd_bus_message_new_signal(bus, &msg, kObjPath, kIface, "DmabufReady");

    if (ret < 0) {
        SPDLOG_DEBUG("failed to set signal {}", ret);
        return false;
    }

    ret = sd_bus_message_set_destination(msg, r.client_id.c_str());
    if (ret < 0) {
        SPDLOG_DEBUG("failed to set destination {}", ret);
        sd_bus_message_unref(msg);
        return false;
    }

    ret = sd_bus_message_append(
        msg,
        "tuuutuuxhhtt",
        fdinfo.modifier,
        fdinfo.dmabuf_offset,
        fdinfo.stride,
        fdinfo.fourcc,
        fdinfo.plane_size,
        fdinfo.w,
        fdinfo.h,
        fdinfo.server_render_minor,
        fdinfo.gbm_fd,
        fdinfo.opaque_fd,
        fdinfo.opaque_size,
        fdinfo.opaque_offset
    );

    if (ret < 0) {
        SPDLOG_DEBUG("failed to append to message {}", ret);
        sd_bus_message_unref(msg);
        return false;
    }

    ret = sd_bus_send(bus, msg, nullptr);
    sd_bus_message_unref(msg);
    if (ret < 0) {
        SPDLOG_DEBUG("failed to send message {}", ret);
        return false;
    }

    return ret < 0 ? ret : 0;
}

bool IPCServer::send_config(std::string& name) {
    SPDLOG_DEBUG("start send config");
    sd_bus_message* msg = nullptr;

    int r = sd_bus_message_new_signal(bus, &msg, kObjPath, kIface, "Config");

    if (r < 0) {
        SPDLOG_DEBUG("failed to set signal {}", r);
        return false;
    }

    r = sd_bus_message_set_destination(msg, name.c_str());
    if (r < 0) {
        SPDLOG_DEBUG("failed to set destination {}", r);
        sd_bus_message_unref(msg);
        return false;
    }

    while (!get_cfg())
        sleep(1);

    r = sd_bus_message_append(msg, "d", get_cfg()->get<double>("fps_limit"));
    SPDLOG_DEBUG("sent fps_limit: {}", get_cfg()->get<double>("fps_limit"));

    if (r < 0) {
        SPDLOG_DEBUG("failed to append to message {}", r);
        sd_bus_message_unref(msg);
        return false;
    }

    r = sd_bus_send(bus, msg, nullptr);
    sd_bus_message_unref(msg);
    if (r < 0) {
        SPDLOG_DEBUG("failed to send message {}", r);
        return false;
    }

    return true;
}

int IPCServer::init() {
    int r = sd_bus_open_user(&bus);
    if (r < 0) {
        SPDLOG_ERROR("bus_open_user failed {} ({}) bus: {}", r, strerror(-r), (void*)bus);
        return r;
    }

    r = sd_bus_add_object_vtable(bus, &slot, kObjPath, kIface, ipc_vtable, this);
    if (r < 0) {
        SPDLOG_ERROR("bus_add_object_vtable failed {} ({}) objpath: {} iface: {}", r, strerror(-r), (void*)bus, kObjPath, kIface);
        return r;
    }

    r = sd_bus_request_name(bus, kBusName, 0);
    if (r < 0) {
        SPDLOG_ERROR("bus_request_name failed {} ({}) bus: {} busName: {}", r, strerror(-r), (void*)bus, kBusName);
        return r;
    }

    std::string match =
        "type='signal',"
        "sender='org.freedesktop.DBus',"
        "path='/org/freedesktop/DBus',"
        "interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',";
        // TODO limit this to our busname
        // "arg0='" + std::string(kBusName) + "'";

    r = sd_bus_add_match(bus, &owner_change_slot, match.c_str(), &IPCServer::on_name_owner_changed, this);
    if (r < 0) {
        SPDLOG_ERROR("match NameOwnerChanged failed: {} ({}) busName: {} ObjPath: {} Iface: {}", r, strerror(-r), kBusName, kObjPath, kIface);
        return r;
    }

    return r;
}

void IPCServer::prune_clients() {
    std::lock_guard<std::mutex> lock(clients_mtx);
    std::deque<std::string> local;
    local.swap(destroy_clients_q);
    for (auto& name : local) {
        if (clients.erase(name) > 0)
            SPDLOG_DEBUG("Client disconnected: {}", name);
    }
}

void IPCServer::dbus_thread() {
    int ir = init();
    if (ir == -17) {
        SPDLOG_INFO("mangohud-server is already running, exiting");
        exit(1);
    }

    SPDLOG_DEBUG("dbus init {} ({})", ir, ir < 0 ? strerror(-ir) : "ok");
    if (ir < 0) return ;

    while (!stop.load()) {
        int r;
        while ((r = sd_bus_process(bus, nullptr)) > 0);

        send_fences();
        send_dmabufs();
        send_configs();
        prune_clients();

        if (r < 0) {
            fprintf(stderr, "process: %d (%s)\n", r, strerror(-r));
        }

        r = sd_bus_wait(bus, 1000);

        if (r < 0) {
            fprintf(stderr, "wait: %d (%s)\n", r, strerror(-r));
            break;
        }
    }

    SPDLOG_DEBUG("Server exited");
}

int IPCServer::handshake(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<IPCServer*>(userdata);
    self->maybe_track_sender(m);
    return sd_bus_reply_method_return(m, "u", kProtoVersion);
}

int IPCServer::frame_samples(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
    auto* self = static_cast<IPCServer*>(userdata);
    auto client = self->get_client(m);
    if (!client)
        return 0;

    int r = sd_bus_message_enter_container(m, 'a', "(tt)");

    std::lock_guard client_lock(client->m);
    for (;;) {
        uint64_t seq = 0, t_ns = 0;
        r = sd_bus_message_read(m, "(tt)", &seq, &t_ns);
        if (r < 0) return r;
        if (r == 0) break;
        client->samples.push_back({seq, t_ns});

        // 500ms windows
        while (client->samples.size() > 2 && (t_ns - client->samples.front().t_ns) > KEEP_NS)
            client->samples.pop_front();

        if (client->have_prev) {
            uint64_t dt_ns = t_ns - client->t_last;
            uint64_t dseq  = seq  - client->seq_last;

            if (dseq > 1)
                client->dropped += (dseq - 1);

            if (dseq > 0 && dt_ns > 0) {
                double ft_ms = (double)dt_ns / (double)dseq / 1e6;
                client->frametimes.push_back(ft_ms);
                if (client->frametimes.size() > FT_MAX)
                    client->frametimes.pop_front();
            }
        } else {
            client->have_prev = true;
        }

        client->t_last = t_ns;
        client->seq_last = seq;
        client->n_frames++;
    }

    r = sd_bus_message_exit_container(m);
    return sd_bus_reply_method_return(m, "");
}

pid_t IPCServer::sender_pid(sd_bus_message* m) {
    sd_bus_creds* creds = nullptr;

    int r = sd_bus_query_sender_creds(m, SD_BUS_CREDS_PID, &creds);
    if (r < 0) return (pid_t)0;

    pid_t pid = 0;
    r = sd_bus_creds_get_pid(creds, &pid);
    sd_bus_creds_unref(creds);

    if (r < 0) return (pid_t)0;
    return pid;
}

int IPCServer::release_fence(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<IPCServer*>(userdata);
    // TODO this might be unsafe without clients mutex
    auto* client = self->get_client(m);
    if (!client) return -EINVAL;

    int msg_fd = -1;
    int r = sd_bus_message_read(m, "h", &msg_fd);

    if (r < 0) return r;
    if (msg_fd < 0) return -EINVAL;

    {
        std::lock_guard lock(client->resources.m);
        int fd = dup(msg_fd);
        if (client->resources.release_fd >= 0) {
            ::close(client->resources.release_fd);
            client->resources.release_fd = -1;
        }

        client->resources.release_fd = fd;
    }
    return 0;
}

void IPCServer::send_fences() {
    std::deque<std::pair<std::string, int>> local;
    {
        std::lock_guard lock(q_mtx);
        local.swap(fences);
    }

    for (auto& [name, fd] : local)
        send_fence(name, fd);
}

void IPCServer::send_dmabufs() {
    std::deque<clientRes*> local;
    {
        std::lock_guard lock(q_mtx);
        local.swap(dmabufs);
    }

    for (auto r : local)
        send_dmabuf(*r);
}

void IPCServer::send_configs() {
    std::deque<std::string> local;
    {
        std::lock_guard lock(q_mtx);
        local.swap(configs);
    }

    for (auto name : local)
        send_config(name);
}
