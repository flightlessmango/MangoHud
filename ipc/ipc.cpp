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
#include "imgui_ctx.h"
#include "vulkan_ctx.h"

Client::Client(std::string name, VulkanContext& vk,
               ImGuiCtx& imgui, pid_t pid,
               uint32_t w = 500, uint32_t h = 500)
                : pid(pid), name(name), vk(vk), imgui(imgui),
                  w(w), h(h), frametimes(200, 0.0f) {
    std::vector<size_t> modifier;
    modifier.push_back(VK_IMAGE_LAYOUT_UNDEFINED);
    gbm = create_gbm_buffer_with_modifiers(
        vk.phys_fd(), w, h, DRM_FORMAT_XRGB8888, modifier);
    vk.create_dmabuf(gbm, target, buf, w, h, fmt);
}

void Client::queue_frame() {
    if (!table)
        return;
    std::unique_lock lock(m);

    frame fr = frame {
        .w = w,
        .h = h,
        .table = *table,
        .target = target,
        .image = buf.image,
        .layout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    imgui.add_to_queue(fr);
}

IPCServer::IPCServer(VulkanContext& vk, ImGuiCtx& imgui, HudTable& table)
                : vk(vk), imgui(imgui), table(table) {
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

    auto it = clients.find(sender);
    if (it == clients.end()) return nullptr;
    return it->second.get();
}

void IPCServer::maybe_track_sender(sd_bus_message *m) {
    const char *sender = sd_bus_message_get_sender(m);
    if (!sender) return;
    bool inserted = false;
    {
        std::lock_guard<std::mutex> lock(clients_mtx);
        auto client = std::make_unique<Client>(sender, vk, imgui, sender_pid(m));
        emit_dmabuf_to(sender, client->gbm);
        inserted = clients.try_emplace(sender, std::move(client)).second;
    }
    if (!inserted) return;

    fprintf(stderr, "Client seen: %s\n", sender);
}

int IPCServer::on_name_owner_changed(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<IPCServer*>(userdata);

    const char *name, *old_owner, *new_owner;
    int r = sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner);
    if (r < 0) return 0;

    if (!name || name[0] != ':') return 0;

    if (new_owner && new_owner[0] == '\0') {
        std::lock_guard<std::mutex> lock(self->clients_mtx);
        if (self->clients.erase(name) > 0)
            fprintf(stderr, "Client disconnected: %s\n", name);
    }

    return 0;
}

int IPCServer::emit_dmabuf_to(std::string name, GbmBuffer& gbm) {
    sd_bus_message* msg = nullptr;
    // Duplicate so we can safely close our copy after sending
    int fd = dup(gbm.fd);
    if (fd < 0) return -errno;

    int r = sd_bus_message_new_signal(bus, &msg, kObjPath, kIface, "DmabufReady");
    if (r < 0) { close(fd); return r; }

    r = sd_bus_message_set_destination(msg, name.c_str());
    if (r < 0) { sd_bus_message_unref(msg); close(fd); return r; }

    // Append payload. Use the same signature layout you already return:
    // fd + width/height/fourcc/modifier/stride/offset/plane_size
    r = sd_bus_message_append(
        msg,
        "huuutuut",
        fd,
        gbm.width,
        gbm.height,
        gbm.fourcc,
        gbm.modifier,
        gbm.stride,
        gbm.offset,
        gbm.plane_size
    );


    if (r < 0) { sd_bus_message_unref(msg); close(fd); return r; }

    r = sd_bus_send(bus, msg, nullptr);
    sd_bus_message_unref(msg);
    if (r >= 0) (void) sd_bus_flush(bus);

    close(fd);
    return r < 0 ? r : 0;
}

static int bus_make_ready(sd_bus* bus) {
    for (;;) {
        int ready = sd_bus_is_ready(bus);
        if (ready > 0) return 0;
        if (ready < 0) return ready;

        int r;
        while ((r = sd_bus_process(bus, nullptr)) > 0) {}
        if (r < 0) return r;

        r = sd_bus_wait(bus, 1000 * 1000);
        if (r < 0) return r;
    }
}

int IPCServer::init() {
    int r = sd_bus_open_user(&bus);
    if (r < 0) return r;

    r = bus_make_ready(bus);
    if (r < 0) return r;

    r = sd_bus_add_object_vtable(bus, &slot, kObjPath, kIface, ipc_vtable, this);
    if (r < 0) return r;

    r = sd_bus_request_name(bus, kBusName, 0);
    if (r < 0) return r;

    sd_bus_add_match(
        bus,
        nullptr,
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'",
        on_name_owner_changed,
        this
    );


    return r;
}

void IPCServer::dbus_thread() {
    int ir = init();
    fprintf(stderr, "init -> %d (%s)\n", ir, ir < 0 ? strerror(-ir) : "ok");
    if (ir < 0) return ;

    while (!stop.load()) {
        int r;

        while ((r = sd_bus_process(bus, nullptr)) > 0) {
        }

        if (r < 0) {
            fprintf(stderr, "process: %d (%s)\n", r, strerror(-r));
            break;
        }

        // r == 0: nothing queued, now block
        r = sd_bus_wait(bus, (uint64_t)-1);
        if (r < 0) {
            fprintf(stderr, "wait: %d (%s)\n", r, strerror(-r));
            break;
        }
    }

}

int IPCServer::handshake(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<IPCServer*>(userdata);
    printf("server handshake, clients.size(): %i\n", (int)self->clients.size());

    const char* info = "";
    (void) sd_bus_message_read(m, "s", &info);

    self->maybe_track_sender(m);
    return sd_bus_reply_method_return(m, "u", kProtoVersion);
}

void IPCServer::queue_all_frames() {
    for (auto& [name, client] : clients)
        client->queue_frame();
}

int IPCServer::set_frame_times(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
    auto* self = static_cast<IPCServer*>(userdata);
    auto client = self->get_client(m);
    if (!client)
        return 0;

        int r = sd_bus_message_enter_container(m, 'a', "d");

    for (;;) {
        double f = 0.f;
        r = sd_bus_message_read(m, "d", &f);
        if (r < 0) return r;
        if (r == 0) break;
        client->frametimes.push_back(f);
        client->frametimes.erase(client->frametimes.begin());
        client->n_frames++;
        client->n_frames_since_update++;
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

bool IPCClient::start() {
    int r = sd_bus_open_user(&bus);
    if (r < 0) {
        fprintf(stderr, "sd_bus_open_user: %d (%s)\n", r, strerror(-r));
        return false;
    }

    r = bus_make_ready(bus);
    if (r < 0) {
        fprintf(stderr, "bus_make_ready: %d (%s)\n", r, strerror(-r));
        sd_bus_unref(bus);
        bus = nullptr;
        return false;
    }

    sd_bus_set_method_call_timeout(bus, 5ULL * 1000ULL * 1000ULL);

    std::string match =
        "type='signal',"
        "sender='" + std::string(kBusName) + "',"
        "path='" + std::string(kObjPath) + "',"
        "interface='" + std::string(kIface) + "',"
        "member='DmabufReady'";

    r = sd_bus_add_match(bus, &dmabuf_slot, match.c_str(), &IPCClient::on_dmabuf_ready, this);
    if (r < 0) {
        fprintf(stderr, "sd_bus_add_match: %d (%s)\n", r, strerror(-r));
        sd_bus_unref(bus);
        bus = nullptr;
        return false;
    }

    quit.store(false);
    thread = std::thread(&IPCClient::bus_thread, this);
    return true;
}

int IPCClient::on_dmabuf_ready(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<IPCClient*>(userdata);

    int msg_fd = -1;
    GbmBuffer incoming{};
    incoming.fd = -1;

    int r = sd_bus_message_read(
        m,
        "huuutuut",
        &msg_fd,
        &incoming.width,
        &incoming.height,
        &incoming.fourcc,
        &incoming.modifier,
        &incoming.stride,
        &incoming.offset,
        &incoming.plane_size
    );
    if (r < 0) {
        fprintf(stderr, "DmabufReady parse: %d (%s)\n", r, strerror(-r));
        return 0;
    }

    int owned = fcntl(msg_fd, F_DUPFD_CLOEXEC, 3);
    if (owned < 0) {
        fprintf(stderr, "DmabufReady dup fd: %d (%s)\n", -errno, strerror(errno));
        return 0;
    }
    incoming.fd = owned;

    {
        std::lock_guard<std::mutex> lock(self->gbm_mtx);
        if (self->gbm.fd >= 0) close(self->gbm.fd);
        self->gbm = incoming;
    }

    fprintf(stderr, "DmabufReady received fd=%d %ux%u fourcc=%u\n",
            owned, incoming.width, incoming.height, incoming.fourcc);

    return 0;
}

void IPCClient::bus_thread() {
    handshake();
    install_server_watch();

    while (!quit.load()) {
        int r;

        while ((r = sd_bus_process(bus, nullptr)) > 0) {}

        if (r < 0) {
            fprintf(stderr, "sd_bus_process: %d (%s)\n", r, strerror(-r));
            break;
        }

        if (need_reconnect.exchange(false))
            handshake();

        // No polling needed once you have the match
        r = sd_bus_wait(bus, (uint64_t)-1);
        if (r < 0) {
            fprintf(stderr, "sd_bus_wait: %d (%s)\n", r, strerror(-r));
            break;
        }
    }
}

void IPCClient::stop() {
    printf("stop\n");
    quit.store(true);
    if (thread.joinable()) thread.join();

    if (dmabuf_slot) sd_bus_slot_unref(dmabuf_slot), dmabuf_slot = nullptr;
    if (bus) sd_bus_unref(bus), bus = nullptr;
    uint64_t one = 1;
    (void)write(quit_fd, &one, sizeof(one));

    std::lock_guard<std::mutex> lock(gbm_mtx);
    if (gbm.fd >= 0) close(gbm.fd);
    gbm.fd = -1;
}

bool IPCClient::handshake() {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    auto cleanup = [&] {
        if (reply) sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
    };

    std::lock_guard<std::mutex> lock(bus_mtx);
    const char* info = "client=gbm; proto=1";
    int r = sd_bus_call_method(
        bus,
        kBusName,
        kObjPath,
        kIface,
        "Handshake",
        &error,
        &reply,
        "s",
        info
    );

    if (r < 0) {
        fprintf(stderr, "Handshake failed: %d (%s) %s: %s\n",
                r, strerror(-r),
                error.name ? error.name : "(no-name)",
                error.message ? error.message : "(no-message)");
        cleanup();
        return false;
    }

    uint32_t proto = 0;
    r = sd_bus_message_read(reply, "u", &proto);
    if (r < 0) {
        fprintf(stderr, "Handshake read failed: %d (%s)\n", r, strerror(-r));
        cleanup();
        return false;
    }

    fprintf(stderr, "Handshake OK proto=%u\n", proto);
    cleanup();
    return true;
}

int IPCClient::on_server_owner_changed(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<IPCClient*>(userdata);

    const char *name = nullptr, *old_owner = nullptr, *new_owner = nullptr;
    int r = sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner);
    if (r < 0) return 0;

    if (!name || strcmp(name, kBusName) != 0) return 0;

    if (new_owner && new_owner[0] == '\0') {
        fprintf(stderr, "server went away\n");
        self->gbm.fd = -1;
        self->server_up.store(false);
        // invalidate any cached fds/state here
    } else {
        fprintf(stderr, "server is up (owner=%s)\n", new_owner);
        self->server_up.store(true);
        self->need_reconnect.store(true);
    }

    return 0;
}

int IPCClient::install_server_watch() {
    std::string match =
        "type='signal',"
        "sender='org.freedesktop.DBus',"
        "path='/org/freedesktop/DBus',"
        "interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',"
        "arg0='" + std::string(kBusName) + "'";

    return sd_bus_add_match(bus, &server_watch, match.c_str(),
                            on_server_owner_changed, this);
}

void IPCClient::drain_queue() {
    queue_t = std::thread(&IPCClient::push_queue, this);
    queue_t.detach();
}

int IPCClient::push_queue() {
    std::deque<float> out;
    std::unique_lock lock(bus_mtx);
    out.swap(frametimes);

    sd_bus_message *m = nullptr;
    int r = sd_bus_message_new_method_call(bus, &m, kBusName, kObjPath, kIface, "SetFrameTimes");
    r = sd_bus_message_open_container(m, 'a', "d");
    for (auto& f : out)
        sd_bus_message_append(m, "d", f);

    sd_bus_message_close_container(m);
    sd_bus_send(bus, m, nullptr);
    sd_bus_message_unref(m);
    return r;
}
