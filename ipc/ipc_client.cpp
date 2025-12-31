#include "ipc.h"
#include "ipc_client.h"

int IPCClient::on_fence_ready(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<IPCClient*>(userdata);
    int msg_fd = -1;
    int r = sd_bus_message_read(m, "h", &msg_fd);
    if (r < 0) {
        SPDLOG_ERROR("FenceReady parse: {} ({})", r , strerror(-r));
        return 0;
    }

    int owned_fd = ::dup(msg_fd);
    if (owned_fd < 0) {
        SPDLOG_ERROR("dup fence failed: {})", r , strerror(errno));
        return 0;
    }

    {
        std::lock_guard lock(self->m);
        if (self->pending_acquire_fd >= 0) ::close(self->pending_acquire_fd);
        self->pending_acquire_fd = owned_fd;
    }

    return 0;
}

int IPCClient::on_config(sd_bus_message* m, void* userdata, sd_bus_error*) {
    printf("got config\n");
    auto* self = static_cast<IPCClient*>(userdata);
    double fps_limit = 0;
    int r = sd_bus_message_read(m, "d", &fps_limit);
    if (r < 0) {
        SPDLOG_ERROR("config parse: {} ({})", r, strerror(-r));
        return 0;
    }

    {
        std::lock_guard lock(self->m);
        self->fps_limit = fps_limit;
    }

    return 0;
}

bool IPCClient::init() {
    int r = sd_bus_open_user(&bus);
    if (r < 0) {
        fprintf(stderr, "sd_bus_open_user: %d (%s)\n", r, strerror(-r));
        return false;
    }

    sd_bus_set_method_call_timeout(bus, 5ULL * 1000ULL * 1000ULL);

    std::string match =
        "type='signal',"
        "sender='org.freedesktop.DBus',"
        "path='/org/freedesktop/DBus',"
        "interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',"
        "arg0='" + std::string(kBusName) + "'";

    sd_bus_add_match(bus, &server_watch, match.c_str(), on_server_owner_changed, this);
    if (r < 0) {
        SPDLOG_ERROR("match server_watch failed: {} ({}) busName: {} ObjPath: {} Iface: {}", r, strerror(-r), kBusName, kObjPath, kIface);
        return false;
    }

    std::string match_dmabuf =
        "type='signal',"
        "sender='" + std::string(kBusName) + "',"
        "path='" + std::string(kObjPath) + "',"
        "interface='" + std::string(kIface) + "',"
        "member='DmabufReady'";

    r = sd_bus_add_match(bus, &dmabuf_slot, match_dmabuf.c_str(), &IPCClient::on_dmabuf_ready, this);
    if (r < 0) {
        SPDLOG_ERROR("match dmabuf failed: {} ({}) busName: {} ObjPath: {} Iface: {}", r, strerror(-r), kBusName, kObjPath, kIface);
        return false;
    }

    std::string match_fence =
        "type='signal',"
        "sender='" + std::string(kBusName) + "',"
        "path='" + std::string(kObjPath) + "',"
        "interface='" + std::string(kIface) + "',"
        "member='FenceReady'";

    r = sd_bus_add_match(bus, &fence_slot, match_fence.c_str(), &IPCClient::on_fence_ready, this);
    if (r < 0) {
        SPDLOG_ERROR("match fence failed: {} ({}) busName: {} ObjPath: {} Iface: {}", r, strerror(-r), kBusName, kObjPath, kIface);
        return false;
    }

    std::string match_config =
        "type='signal',"
        "sender='" + std::string(kBusName) + "',"
        "path='" + std::string(kObjPath) + "',"
        "interface='" + std::string(kIface) + "',"
        "member='Config'";

    r = sd_bus_add_match(bus, &config_slot, match_config.c_str(), &IPCClient::on_config, this);
    if (r < 0) {
        SPDLOG_ERROR("match config failed: {} ({}) busName: {} ObjPath: {} Iface: {}", r, strerror(-r), kBusName, kObjPath, kIface);
        return false;
    }

    quit.store(false);
    return true;
}

int IPCClient::on_dmabuf_ready(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<IPCClient*>(userdata);
    SPDLOG_DEBUG("got dmabuf");

    int gbm_fd = -1;
    int opaque_fd = -1;
    Fdinfo fdinfo;

    int r = sd_bus_message_read(
        m,
        "tuuutuuxhhtt",
        &fdinfo.modifier,
        &fdinfo.dmabuf_offset,
        &fdinfo.stride,
        &fdinfo.fourcc,
        &fdinfo.plane_size,
        &fdinfo.w,
        &fdinfo.h,
        &fdinfo.server_render_minor,
        &gbm_fd,
        &opaque_fd,
        &fdinfo.opaque_size,
        &fdinfo.opaque_offset
    );

    if (r < 0) {
        fprintf(stderr, "DmabufReady parse: %d (%s)\n", r, strerror(-r));
        return 0;
    }

    int owned_dma = dup(gbm_fd);
    if (owned_dma < 0) {
        fprintf(stderr, "DmabufReady dup dma fd: %d (%s)\n", -errno, strerror(errno));
        return 0;
    }
    fdinfo.gbm_fd = owned_dma;

    int owned_opaque = dup(opaque_fd);
    fdinfo.opaque_fd = owned_opaque;

    {
        std::lock_guard lock(self->m);
        self->fdinfo = fdinfo;
        self->needs_import.store(true);
    }

    return 0;
}

void IPCClient::bus_thread() {
    uint64_t next_send = 0;
    const uint64_t period = 1000000000ULL / 90;

    if (!init())
        SPDLOG_ERROR("dbus init failed\n");

    handshake();
    int r = 0;
    while (!quit.load()) {
        uint64_t now = os_time_get_nano();
        if (next_send == 0) next_send = now + period;

        for (;;) {
            r = sd_bus_process(bus, nullptr);
            if (r < 0)
                SPDLOG_ERROR("bus_process failed: {} ({})", r, strerror(-r));

            if (r <= 0)
                break;
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

        std::deque<int> local_fences;
        {
            std::lock_guard lock(fences_mtx);
            local_fences.swap(fences);
        }
        for (auto& fence : local_fences)
            send_release_fence(fence);

        uint64_t wait_ns = (next_send > now) ? (next_send - now) : 0;

        r = sd_bus_wait(bus, wait_ns / 1000);
        if (r < 0) {
            SPDLOG_ERROR("bus_wait failed: {} ({}) wait_ns: {}", r, strerror(-r), wait_ns);
            break;
        }
    }
    SPDLOG_DEBUG("IPC server quit");
}

void IPCClient::stop() {
    quit.store(true);
    if (thread.joinable()) thread.join();

    if (dmabuf_slot) sd_bus_slot_unref(dmabuf_slot), dmabuf_slot = nullptr;
    if (bus) sd_bus_unref(bus), bus = nullptr;


    std::lock_guard<std::mutex> lock(m);
    if (fdinfo.gbm_fd >= 0) close(fdinfo.gbm_fd);
        fdinfo.gbm_fd = -1;

    if (fdinfo.opaque_fd >= 0) close(fdinfo.opaque_fd);
        fdinfo.opaque_fd = -1;
}

bool IPCClient::handshake() {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    auto cleanup = [&] {
        if (reply) sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
    };

    std::lock_guard<std::mutex> lock(bus_mtx);
    int r = sd_bus_call_method(
        bus,
        kBusName,
        kObjPath,
        kIface,
        "Handshake",
        &error,
        &reply,
        "sx",
        pEngineName.c_str(),
        renderMinor
    );

    if (r < 0) {
        SPDLOG_ERROR("Failed to send handshake {} ({})", r, strerror(-r));
        cleanup();
        return false;
    }

    uint32_t proto = 0;
    r = sd_bus_message_read(reply, "u", &proto);
    if (r < 0) {
        SPDLOG_ERROR("Handshake read failed {} ({})", r, strerror(-r));
        cleanup();
        return false;
    }

    SPDLOG_INFO("Handshake OK");
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
        SPDLOG_DEBUG("Server went away");

        self->need_reconnect.store(true);
    } else {
        self->need_reconnect.store(true);
    }

    return 0;
}

int IPCClient::push_queue() {
    std::deque<Sample> out;
    {
        std::lock_guard<std::mutex> lock(samples_mtx);
        out.swap(samples);
    }

    if (out.empty())
        return 0;

    sd_bus_message* m = nullptr;

    int r = sd_bus_message_new_method_call(bus, &m, kBusName, kObjPath, kIface, "FrameSamples");
    if (r < 0) {
        SPDLOG_ERROR("push_queue: new_method_call failed {} ({})", r, strerror(-r));
        return r < 0 ? r : -EIO;
    }

    r = sd_bus_message_open_container(m, 'a', "(tt)");
    if (r < 0) {
        SPDLOG_ERROR("push_queue: open_container failed {} ({})", r, strerror(-r));
        sd_bus_message_unref(m);
        return r;
    }

    for (auto& [seq, now] : out) {
        r = sd_bus_message_append(m, "(tt)", (uint64_t)seq, (uint64_t)now);
        if (r < 0) {
            SPDLOG_ERROR("push_queue: append failed {} ({})", r, strerror(-r));
            sd_bus_message_unref(m);
            return r;
        }
    }

    r = sd_bus_message_close_container(m);
    if (r < 0) {
        SPDLOG_ERROR("push_queue: close_container failed {} ({})", r, strerror(-r));
        sd_bus_message_unref(m);
        return r;
    }

    r = sd_bus_send(bus, m, nullptr);
    if (r < 0) {
        SPDLOG_ERROR("push_queue: sd_bus_send failed {} ({})", r, strerror(-r));
        sd_bus_message_unref(m);
        return r;
    }

    sd_bus_message_unref(m);
    return 0;
}

int IPCClient::send_release_fence(int fd) {
    if (fd < 0) return 0;
    sd_bus_message* m = nullptr;

    int r = sd_bus_message_new_method_call(bus, &m, kBusName, kObjPath, kIface, "ReleaseFence");
    if (r < 0) {
        SPDLOG_ERROR("send_release: new_method_call failed {} ({})", r, strerror(-r));
        ::close(fd);
        return r;
    }

    r = sd_bus_message_append(m, "h", fd);
    if (r < 0) {
        SPDLOG_ERROR("send_release: append failed {} ({})", r, strerror(-r));
        sd_bus_message_unref(m);
        ::close(fd);
        return r;
    }
    ::close(fd);

    r = sd_bus_send(bus, m, nullptr);
    if (r < 0) {
        SPDLOG_ERROR("send_release: send failed {} ({})", r, strerror(-r));
    }
    sd_bus_message_unref(m);
    return r < 0 ? r : 0;
}

int IPCClient::ready_frame() {
    std::lock_guard lock(m);
    if (!sync_fd_signaled(pending_acquire_fd))
        return -1;

    int fd = pending_acquire_fd;
    pending_acquire_fd = -1;
    return fd;
}

