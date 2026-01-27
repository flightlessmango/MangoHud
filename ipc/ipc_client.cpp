#include "ipc.h"
#include "ipc_client.h"

int IPCClient::on_fence(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<IPCClient*>(userdata);
    int msg_fd = -1;
    int r = sd_bus_message_read(m, "h", &msg_fd);
    if (r < 0) {
        SPDLOG_ERROR("fence: {} ({})", r , strerror(-r));
        return 0;
    }

    int owned_fd = ::dup(msg_fd);
    if (owned_fd < 0) {
        SPDLOG_ERROR("dup fence: {})", r , strerror(errno));
        return 0;
    }

    {
        std::lock_guard lock(self->m);
        if (self->pending_acquire_fd >= 0)
            ::close(self->pending_acquire_fd);

        self->pending_acquire_fd = owned_fd;
    }

    return 0;
}

int IPCClient::on_config(sd_bus_message* m, void* userdata, sd_bus_error*) {
    SPDLOG_DEBUG("got config");
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

int IPCClient::on_dmabuf(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<IPCClient*>(userdata);
    SPDLOG_DEBUG("got dmabuf");

    int gbm_fd = -1;
    int opaque_fd = -1;
    bool has_gbm = false;
    Fdinfo fdinfo;

    int r = sd_bus_message_read(
        m,
        "tuuutuuxbhhtt",
        &fdinfo.modifier,
        &fdinfo.dmabuf_offset,
        &fdinfo.stride,
        &fdinfo.fourcc,
        &fdinfo.plane_size,
        &fdinfo.w,
        &fdinfo.h,
        &fdinfo.server_render_minor,
        &has_gbm,
        &gbm_fd,
        &opaque_fd,
        &fdinfo.opaque_size,
        &fdinfo.opaque_offset
    );

    if (r < 0) {
        fprintf(stderr, "dmabuf parse: %d (%s)\n", r, strerror(-r));
        return 0;
    }

    int owned_dma = dup(gbm_fd);
    if (owned_dma < 0) {
        fprintf(stderr, "dmabuf dup dma fd: %d (%s)\n", -errno, strerror(errno));
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

void IPCClient::disconnect_bus() {
    if (dmabuf_slot) {
        sd_bus_slot_unref(dmabuf_slot);
        dmabuf_slot = nullptr;
    }
    if (fence_slot) {
        sd_bus_slot_unref(fence_slot);
        fence_slot = nullptr;
    }
    if (config_slot) {
        sd_bus_slot_unref(config_slot);
        config_slot = nullptr;
    }
    if (bus) {
        sd_bus_unref(bus);
        bus = nullptr;
    }
}

bool IPCClient::connect_bus() {
    int fd = request_fd_from_server();
    if (fd < 0)
        return false;

    sd_bus *b = nullptr;
    int r = sd_bus_new(&b);
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_new: {} ({})", r, strerror(-r));
        return false;
    }

    r = sd_bus_set_fd(b, fd, fd);
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_set_fd: {} ({})", r, strerror(-r));
        sd_bus_unref(b);
        return false;
    }

    r = sd_bus_start(b);
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_start: {} ({})", r, strerror(-r));
        sd_bus_unref(b);
        return false;
    }

    std::string match_dmabuf =
        "type='signal',"
        "path='" + std::string(kObjPath) + "',"
        "interface='" + std::string(kIface) + "',"
        "member='dmabuf'";

    r = sd_bus_add_match(b, &dmabuf_slot, match_dmabuf.c_str(), &IPCClient::on_dmabuf, this);
    if (r < 0) {
        SPDLOG_ERROR("match dmabuf: {} ({})", r, strerror(-r));
        sd_bus_unref(b);
        return false;
    }

    std::string match_fence =
        "type='signal',"
        "path='" + std::string(kObjPath) + "',"
        "interface='" + std::string(kIface) + "',"
        "member='fence'";

    r = sd_bus_add_match(b, &fence_slot, match_fence.c_str(), &IPCClient::on_fence, this);
    if (r < 0) {
        SPDLOG_ERROR("match fence: {} ({})", r, strerror(-r));
        sd_bus_unref(b);
        return false;
    }

    std::string match_config =
        "type='signal',"
        "path='" + std::string(kObjPath) + "',"
        "interface='" + std::string(kIface) + "',"
        "member='config'";

    r = sd_bus_add_match(b, &config_slot, match_config.c_str(), &IPCClient::on_config, this);
    if (r < 0) {
        SPDLOG_ERROR("match config: {} ({})", r, strerror(-r));
        sd_bus_unref(b);
        return false;
    }

    bus = b;
    return true;
}

bool IPCClient::run_bus() {
    while (!quit.load()) {
        int fd = sd_bus_get_fd(bus);
        if (fd < 0) {
            return false;
        }

        int events = sd_bus_get_events(bus);
        if (events < 0) {
            return false;
        }

        pollfd pfds[2]{};
        pfds[0].fd = fd;
        pfds[0].events = static_cast<short>(events);

        pfds[1].fd = wake_fd;
        pfds[1].events = POLLIN;

        int pr = poll(pfds, 2, -1);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (pfds[1].revents & POLLIN) {
            uint64_t v = 0;
            (void)read(wake_fd, &v, sizeof(v));

            if (quit.load())
                break;
        }

        if (pfds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            return false;
        }

        for (;;) {
            int rr = sd_bus_process(bus, nullptr);

            push_queue();

            std::deque<int> local;
            {
                std::lock_guard lock(fences_mtx);
                local.swap(fences);
            }
            for (auto& fence : local)
                send_release_fence(fence);

            if (rr < 0)
                return false;

            if (rr == 0)
                break;
        }
    }
    return true;
}

void IPCClient::bus_thread() {
    quit.store(false);
    while (!quit.load()) {
        disconnect_bus();

        if (!connect_bus()) {
            sleep(1);
            continue;
        }

        SPDLOG_DEBUG("IPC connected");
        on_connect();

        run_bus();

        if (!quit.load()) {
            connected.store(false);
            SPDLOG_DEBUG("IPC disconnected, reconnecting");
        }
    }

    disconnect_bus();
    SPDLOG_DEBUG("IPC client thread exit");
}

void IPCClient::stop() {
    quit.store(true);
    uint64_t one = 1;
    (void)write(wake_fd, &one, sizeof(one));

    if (thread.joinable()) thread.join();

    if (dmabuf_slot) sd_bus_slot_unref(dmabuf_slot), dmabuf_slot = nullptr;
    if (bus) sd_bus_unref(bus), bus = nullptr;


    std::lock_guard<std::mutex> lock(m);
    if (fdinfo.gbm_fd >= 0) close(fdinfo.gbm_fd);
        fdinfo.gbm_fd = -1;

    if (fdinfo.opaque_fd >= 0) close(fdinfo.opaque_fd);
        fdinfo.opaque_fd = -1;
}

bool IPCClient::on_connect() {
    std::lock_guard<std::mutex> lock(bus_mtx);

    if (!bus) {
        return false;
    }

    int r = sd_bus_emit_signal(
        bus,
        kObjPath,
        kIface,
        "on_connect",
        "sx",
        pEngineName.c_str(),
        (int64_t)renderMinor
    );

    if (r < 0) {
        SPDLOG_ERROR("on_connect signal send {} ({})", r, strerror(-r));
        return false;
    }

    SPDLOG_INFO("on_connect signal sent");
    connected.store(true);
    return true;
}

int IPCClient::push_queue() {
    if (samples.empty() || !connected.load())
        return 0;

    std::deque<Sample> out;
    {
        std::lock_guard<std::mutex> lock(samples_mtx);
        out.swap(samples);
    }

    sd_bus_message* m = nullptr;

    int r = sd_bus_message_new_signal(bus, &m, kObjPath, kIface, "frame_samples");
    if (r < 0) {
        SPDLOG_ERROR("push_queue: new_signal {} ({})", r, strerror(-r));
        return r;
    }

    r = sd_bus_message_open_container(m, 'a', "(tt)");
    if (r < 0) {
        SPDLOG_ERROR("push_queue: open_container {} ({})", r, strerror(-r));
        sd_bus_message_unref(m);
        return r;
    }

    for (auto& [seq, now] : out) {
        r = sd_bus_message_append(m, "(tt)", (uint64_t)seq, (uint64_t)now);
        if (r < 0) {
            SPDLOG_ERROR("push_queue: append {} ({})", r, strerror(-r));
            sd_bus_message_unref(m);
            return r;
        }
    }

    r = sd_bus_message_close_container(m);
    if (r < 0) {
        SPDLOG_ERROR("push_queue: close_container {} ({})", r, strerror(-r));
        sd_bus_message_unref(m);
        return r;
    }

    r = sd_bus_send(bus, m, nullptr);
    if (r < 0) {
        SPDLOG_ERROR("push_queue: sd_bus_send {} ({})", r, strerror(-r));
        sd_bus_message_unref(m);
        return r;
    }

    sd_bus_message_unref(m);
    return 0;
}

int IPCClient::send_release_fence(int fd) {
    std::lock_guard<std::mutex> lock(bus_mtx);

    if (!bus)
        return false;

    int r = sd_bus_emit_signal(
        bus,
        kObjPath,
        kIface,
        "release_fence",
        "h",
        fd
    );

    close (fd);

    if (r < 0) {
        SPDLOG_ERROR("release_fence signal send {} ({})", r, strerror(-r));
        return false;
    }

    return true;
}

int IPCClient::ready_frame() {
    std::lock_guard lock(m);
    if (!sync_fd_signaled(pending_acquire_fd))
        return -1;

    int fd = pending_acquire_fd;
    pending_acquire_fd = -1;
    return fd;
}

int IPCClient::request_fd_from_server() {
    if (socket_fd >= 0)
        close(socket_fd);

    sd_bus *bus_ = nullptr;
    int r = sd_bus_open_user(&bus_);
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_open_user {} ({})", r, strerror(-r));
        return r;
    }

    sd_bus_message *reply = nullptr;
    r = sd_bus_call_method(bus_, kBusName, kObjPath, kIface, "request_fd", nullptr, &reply, "");
    if (r < 0) {
        if (r != -113)
            SPDLOG_ERROR("sd_bus_call_method: {} ({})", r, strerror(-r));

        sd_bus_unref(bus_);
        return r;
    }

    int fd_msg = -1;
    r = sd_bus_message_read(reply, "h", &fd_msg);
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_message_read {} ({})", r, strerror(-r));
        sd_bus_message_unref(reply);
        sd_bus_unref(bus_);
        return r;
    }

    int fd_copy = dup(fd_msg);
    if (fd_copy < 0) {
        SPDLOG_ERROR("dup failed {} ({})", errno, strerror(errno));
        sd_bus_message_unref(reply);
        sd_bus_unref(bus_);
        return -errno;
    }

    sd_bus_message_unref(reply);
    sd_bus_unref(bus_);

    socket_fd = fd_copy;
    return socket_fd;
}

