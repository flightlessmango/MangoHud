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
                : pid(pid), frametimes(200, 0.0f), name(name)
                , vk(vk), imgui(imgui), w(w), h(h) {
    std::vector<uint64_t> modifier;
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
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .client_id = name
    };
    fr.acquire_fd = acquire_fence();
    imgui.add_to_queue(std::move(fr));
}

int Client::acquire_fence() {
    int fd = pending_acquire_fd;
    pending_acquire_fd = -1;
    return fd;
}

int Client::create_sync_fd() {
    return 0;
}

IPCServer::IPCServer(VulkanContext& vk, ImGuiCtx& imgui)
                : vk(vk), imgui(imgui) {
    thread = std::thread(&IPCServer::dbus_thread, this);
}

IPCServer::~IPCServer() {
    printf("ipc server stopping\n");
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
        self->imgui.client_states.erase(name);
    }

    return 0;
}

int IPCServer::emit_dmabuf_to(std::string name, GbmBuffer& gbm) {
    sd_bus_message* msg = nullptr;
    int dma_fd = dup(gbm.fd);
    if (dma_fd < 0) return -errno;

    int r = sd_bus_message_new_signal(bus, &msg, kObjPath, kIface, "DmabufReady");

    r = sd_bus_message_set_destination(msg, name.c_str());
    r = sd_bus_message_append(
        msg,
        "huuutuut",
        dma_fd,
        gbm.width,
        gbm.height,
        gbm.fourcc,
        gbm.modifier,
        gbm.stride,
        gbm.offset,
        gbm.plane_size
    );

    printf("send buf\n");
    r = sd_bus_send(bus, msg, nullptr);
    sd_bus_message_unref(msg);
    if (r >= 0) (void) sd_bus_flush(bus);

    close(dma_fd);
    return r < 0 ? r : 0;
}

int IPCServer::emit_fence_to(std::string& name, int fd) {
    sd_bus_message* msg = nullptr;

    int r = sd_bus_message_new_signal(bus, &msg, kObjPath, kIface, "FenceReady");
    if (r < 0) return r;

    r = sd_bus_message_set_destination(msg, name.c_str());
    if (r < 0) { sd_bus_message_unref(msg); return r; }

    r = sd_bus_message_append(msg, "h", fd);
    if (r < 0) { sd_bus_message_unref(msg); return r; }

    r = sd_bus_send(bus, msg, nullptr);
    sd_bus_message_unref(msg);

    if (r < 0) return r;
    return 0;
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

        while ((r = sd_bus_process(bus, nullptr)) > 0) {}

        if (r < 0) {
            fprintf(stderr, "process: %d (%s)\n", r, strerror(-r));
        }

        std::deque<std::pair<std::string, int>> local_fences;
        {
            std::unique_lock lock(fences_mtx);
            local_fences.swap(fences);
        }
        for (auto& [name, fd] : local_fences)
            emit_fence_to(name, fd);

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

int IPCServer::frame_samples(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
    auto* self = static_cast<IPCServer*>(userdata);
    auto client = self->get_client(m);
    if (!client)
        return 0;

    int r = sd_bus_message_enter_container(m, 'a', "(tt)");

    std::unique_lock client_lock(client->m);
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

int IPCClient::on_fence_ready(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
    auto* self = static_cast<IPCClient*>(userdata);

    int msg_fd = -1;
    int r = sd_bus_message_read(m, "h", &msg_fd);
    if (r < 0) {
        fprintf(stderr, "FenceReady parse: %d (%s)\n", r, strerror(-r));
        return 0;
    }

    int owned_fd = ::dup(msg_fd);
    if (owned_fd < 0) {
        fprintf(stderr, "dup fence failed: %s\n", strerror(errno));
        return 0;
    }

    {
        std::lock_guard<std::mutex> lock(self->gbm_mtx);

        if (self->pending_acquire_fd >= 0) {
            ::close(self->pending_acquire_fd);
        }

        self->pending_acquire_fd = owned_fd;
        self->have_new_frame.store(true, std::memory_order_release);
    }

    return 0;
}

bool IPCClient::init() {
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

    std::string match_dmabuf =
        "type='signal',"
        "sender='" + std::string(kBusName) + "',"
        "path='" + std::string(kObjPath) + "',"
        "interface='" + std::string(kIface) + "',"
        "member='DmabufReady'";

    sd_bus_add_match(bus, &dmabuf_slot, match_dmabuf.c_str(), &IPCClient::on_dmabuf_ready, this);

    std::string match_fence =
        "type='signal',"
        "sender='" + std::string(kBusName) + "',"
        "path='" + std::string(kObjPath) + "',"
        "interface='" + std::string(kIface) + "',"
        "member='FenceReady'";

    sd_bus_add_match(bus, &dmabuf_slot, match_fence.c_str(), &IPCClient::on_fence_ready, this);

    quit.store(false);
    return true;
}

int IPCClient::on_dmabuf_ready(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<IPCClient*>(userdata);
    printf("got dmabuf\n");

    int msg_fd = -1;
    GbmBuffer incoming{};

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

    int owned_dma = fcntl(msg_fd, F_DUPFD_CLOEXEC, 3);
    if (owned_dma < 0) {
        fprintf(stderr, "DmabufReady dup dma fd: %d (%s)\n", -errno, strerror(errno));
        return 0;
    }
    incoming.fd = owned_dma;

    {
        std::lock_guard<std::mutex> lock(self->gbm_mtx);
        if (self->gbm.fd >= 0) close(self->gbm.fd);
        self->gbm = incoming;
    }

    return 0;
}

void IPCClient::bus_thread() {
    uint64_t next_send = 0;
    const uint64_t period = 1000000000ULL / 90;

    init();
    handshake();
    install_server_watch();

    while (!quit.load()) {
        uint64_t now = os_time_get_nano();
        if (next_send == 0) next_send = now + period;

        // Process pending bus messages (bus thread owns this)
        for (;;) {
            int r = sd_bus_process(bus, nullptr);
            if (r <= 0) break;
        }

        if (need_reconnect.exchange(false))
            handshake();

        now = os_time_get_nano();
        if (now >= next_send) {
            do {
                push_queue();
                next_send += period;
            } while (now >= next_send);
        }

        uint64_t wait_ns = (next_send > now) ? (next_send - now) : 0;

        sd_bus_wait(bus, wait_ns / 1000);
    }
}


void IPCClient::stop() {
    printf("stop\n");
    quit.store(true);
    if (thread.joinable()) thread.join();

    if (dmabuf_slot) sd_bus_slot_unref(dmabuf_slot), dmabuf_slot = nullptr;
    if (bus) sd_bus_unref(bus), bus = nullptr;


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

int IPCClient::push_queue() {
    std::deque<Sample> out;
    {
        std::lock_guard<std::mutex> lock(samples_mtx);
        out.swap(samples);
    }

    if (out.empty())
        return 0;

    if (!bus) {
        fprintf(stderr, "push_queue: bus is null\n");
        return -EINVAL;
    }

    sd_bus_message* m = nullptr;

    int r = sd_bus_message_new_method_call(bus, &m, kBusName, kObjPath, kIface, "FrameSamples");
    if (r < 0 || !m) {
        fprintf(stderr, "push_queue: new_method_call failed r=%d\n", r);
        return r < 0 ? r : -EIO;
    }

    r = sd_bus_message_open_container(m, 'a', "(tt)");
    if (r < 0) {
        fprintf(stderr, "push_queue: open_container failed r=%d\n", r);
        sd_bus_message_unref(m);
        return r;
    }

    for (auto& [seq, now] : out) {
        r = sd_bus_message_append(m, "(tt)", (uint64_t)seq, (uint64_t)now);
        if (r < 0) {
            fprintf(stderr, "push_queue: append failed r=%d\n", r);
            sd_bus_message_unref(m);
            return r;
        }
    }

    r = sd_bus_message_close_container(m);
    if (r < 0) {
        fprintf(stderr, "push_queue: close_container failed r=%d\n", r);
        sd_bus_message_unref(m);
        return r;
    }

    r = sd_bus_send(bus, m, nullptr);
    if (r < 0) {
        fprintf(stderr, "push_queue: sd_bus_send failed r=%d\n", r);
        sd_bus_message_unref(m);
        return r;
    }

    sd_bus_message_unref(m);
    return 0;
}


int IPCClient::send_acquire_fence(int fd) {
    // TODO we need to send this from the main thread or the bus will not be happy
    // sd_bus_message* m = nullptr;

    // int r = sd_bus_message_new_method_call(bus, &m, kBusName, kObjPath, kIface, "AcquireFence");
    // if (r < 0) return r;

    // // If you want to keep your own fd open, dup it here. Otherwise you can pass fd directly.
    // r = sd_bus_message_append(m, "h", fd);
    // if (r < 0) { sd_bus_message_unref(m); return r; }

    // r = sd_bus_send(bus, m, nullptr);
    // sd_bus_message_unref(m);
    return 0;
}

int IPCServer::acquire_fence(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
    auto* self = static_cast<IPCServer*>(userdata);
    auto* client = self->get_client(m);
    if (!client) return -EINVAL;

    int msg_fd = -1;
    int r = sd_bus_message_read(m, "h", &msg_fd);
    if (r < 0) return r;

    int owned_fd = fcntl(msg_fd, F_DUPFD_CLOEXEC, 3);
    if (owned_fd < 0) return -errno;

    {
        std::lock_guard<std::mutex> lock(client->m);
        if (client->pending_acquire_fd >= 0) {
            ::close(client->pending_acquire_fd);
        }
        client->pending_acquire_fd = owned_fd;
    }

    return 0;
}
