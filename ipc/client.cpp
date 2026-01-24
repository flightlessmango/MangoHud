#include "client.h"
#include "ipc.h"

class IPCServer;

clientRes::~clientRes() {
    destroy_client_res(device, *this);
}

void clientRes::reset() {
    destroy_client_res(device, *this);
    send_dmabuf = false;
    reinit_dmabuf = false;
    initialized = false;
    initial_fence = true;
}

bool Client::ready_frame() {
    std::lock_guard lock(resources.m);
    if (resources.release_fd < 0 && resources.initial_fence)
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

int Client::on_stop_event(sd_event_source *s, int fd, uint32_t revents, void *userdata) {
    (void)s;
    (void)revents;

    uint64_t v = 0;
    (void)read(fd, &v, sizeof(v));

    sd_event *e = sd_event_source_get_event(s);
    sd_event_exit(e, 0);
    return 0;
}

int Client::on_bus_disconnected(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    (void)m;
    (void)ret_error;

    auto *self = static_cast<Client *>(userdata);
    SPDLOG_DEBUG("Client disconnected: {}", self->pid);
    self->set_dead();
    self->stop.store(true);
    return 0;
}

void Client::dbus_thread() {
    send_config();

    event = nullptr;
    int r = sd_event_new(&event);
    if (r < 0) {
        SPDLOG_ERROR("sd_event_new {} ({})", r, strerror(-r));
        set_dead();
        return;
    }

    r = sd_bus_attach_event(bus, event, 0);
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_attach_event {} ({})", r, strerror(-r));
        sd_event_unref(event);
        event = nullptr;
        set_dead();
        return;
    }

    sd_event_source *stop_src = nullptr;
    r = sd_event_add_io(event, &stop_src, stop_eventfd, EPOLLIN, &Client::on_stop_event, this);
    if (r < 0) {
        SPDLOG_ERROR("sd_event_add_io(stop_eventfd) {} ({})", r, strerror(-r));
        sd_event_unref(event);
        event = nullptr;
        set_dead();
        return;
    }

    r = sd_event_loop(event);
    if (r < 0 && !stop.load())
        SPDLOG_ERROR("sd_event_loop {} ({})", r, strerror(-r));

    sd_event_source_unref(stop_src);
    sd_event_unref(event);
    event = nullptr;
}

int Client::on_connect(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
    SPDLOG_DEBUG("on_connect");
    auto* self = static_cast<Client*>(userdata);
    const char* EngineName = "";
    int64_t renderMinor;
    int r = sd_bus_message_read(m, "sx", &EngineName, &renderMinor);
    if (r < 0) {
        SPDLOG_ERROR("on_connect read {} ({})", r, strerror(-r));
        self->set_dead();
    }

    self->pEngineName = EngineName;
    self->renderMinor = renderMinor;

    return 0;
}

bool Client::send_dmabuf(){
    Fdinfo fdinfo;
    {
        resources.send_dmabuf       = false;
        fdinfo.modifier             = resources.gbm.modifier;
        fdinfo.dmabuf_offset        = resources.gbm.offset;
        fdinfo.stride               = resources.gbm.stride;
        fdinfo.fourcc               = resources.gbm.fourcc;
        fdinfo.plane_size           = resources.gbm.plane_size;
        fdinfo.w                    = resources.w;
        fdinfo.h                    = resources.h;
        fdinfo.server_render_minor  = resources.server_render_minor;
        fdinfo.opaque_fd            = resources.opaque_fd;
        fdinfo.opaque_size          = resources.opaque_size;
        fdinfo.opaque_offset        = resources.opaque_offset;
        if (resources.gbm.fd < 0) {
            fdinfo.has_gbm  = false;
            fdinfo.gbm_fd = open("/dev/null", O_RDONLY);
        } else {
            fdinfo.has_gbm = true;
            fdinfo.gbm_fd = resources.gbm.fd;
        }
    }

    sd_bus_message* msg = nullptr;
    int ret = sd_bus_message_new_signal(bus, &msg, kObjPath, kIface, "dmabuf");

    SPDLOG_DEBUG("has_gbm={} gbm_fd={} opaque_fd={}",
             (int)fdinfo.has_gbm, fdinfo.gbm_fd, fdinfo.opaque_fd);

    if (ret < 0) {
        SPDLOG_DEBUG("failed to set signal {}", ret);
        return false;
    }

    ret = sd_bus_message_append(
        msg,
        "tuuutuuxbhhtt",
        fdinfo.modifier,
        fdinfo.dmabuf_offset,
        fdinfo.stride,
        fdinfo.fourcc,
        fdinfo.plane_size,
        fdinfo.w,
        fdinfo.h,
        fdinfo.server_render_minor,
        (int)fdinfo.has_gbm,
        fdinfo.gbm_fd,
        fdinfo.opaque_fd,
        fdinfo.opaque_size,
        fdinfo.opaque_offset
    );

    if (ret < 0) {
        SPDLOG_DEBUG("failed to append to message {} ({})", ret, strerror(-ret));
        sd_bus_message_unref(msg);
        return false;
    }

    ret = sd_bus_send(bus, msg, nullptr);
    sd_bus_message_unref(msg);
    if (ret < 0) {
        set_dead();
        SPDLOG_DEBUG("failed to send message {}", ret);
        return false;
    }

    return ret < 0 ? ret : 0;
}

bool Client::send_config() {
    sd_bus_message* msg = nullptr;

    int r = sd_bus_message_new_signal(bus, &msg, kObjPath, kIface, "config");

    if (r < 0) {
        SPDLOG_DEBUG("failed to set signal {}", r);
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
        set_dead();
        SPDLOG_DEBUG("failed to send message {}", r);
        return false;
    }

    return true;
}

bool Client::send_fence() {
    if (resources.acquire_fd < 0)
        return false;

    sd_bus_message* msg = nullptr;

    int r = sd_bus_message_new_signal(bus, &msg, kObjPath, kIface, "fence");

    if (r < 0) {
        SPDLOG_ERROR("sd_bus_message_new_signal {} ({})", r, strerror(-r));
        close(resources.acquire_fd);
        return false;
    }

    r = sd_bus_message_append(msg, "h", resources.acquire_fd);
    close(resources.acquire_fd);
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_message_append {} ({})", r, strerror(-r));
        sd_bus_message_unref(msg);
        return false;
    }

    r = sd_bus_send(bus, msg, nullptr);
    sd_bus_message_unref(msg);
    if (r < 0) {
        if (r != -107)
            SPDLOG_ERROR("sd_bus_send {} ({})", r, strerror(-r));

        return false;
    }

    resources.initial_fence = false;
    resources.acquire_fd = -1;
    return true;
}

int Client::release_fence(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<Client*>(userdata);

    int msg_fd = -1;
    int r = sd_bus_message_read(m, "h", &msg_fd);

    if (r < 0) return r;
    if (msg_fd < 0) return -EINVAL;

    {
        std::lock_guard lock(self->resources.m);
        int fd = dup(msg_fd);
        if (self->resources.release_fd >= 0) {
            ::close(self->resources.release_fd);
            self->resources.release_fd = -1;
        }

        self->resources.release_fd = fd;
    }
    return 0;
}

int Client::frame_samples(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
    auto* self = static_cast<Client*>(userdata);
    int r = sd_bus_message_enter_container(m, 'a', "(tt)");

    std::lock_guard client_lock(self->m);
    for (;;) {
        uint64_t seq = 0, t_ns = 0;
        r = sd_bus_message_read(m, "(tt)", &seq, &t_ns);
        if (r < 0) return r;
        if (r == 0) break;
        self->samples.push_back({seq, t_ns});

        // 500ms windows
        while (self->samples.size() > 2 && (t_ns - self->samples.front().t_ns) > KEEP_NS)
            self->samples.pop_front();

        if (self->have_prev) {
            uint64_t dt_ns = t_ns - self->t_last;
            uint64_t dseq  = seq  - self->seq_last;

            if (dseq > 1)
                self->dropped += (dseq - 1);

            if (dseq > 0 && dt_ns > 0) {
                double ft_ms = (double)dt_ns / (double)dseq / 1e6;
                self->frametimes.push_back(ft_ms);
                if (self->frametimes.size() > FT_MAX)
                    self->frametimes.pop_front();
            }
        } else {
            self->have_prev = true;
        }

        self->t_last = t_ns;
        self->seq_last = seq;
        self->n_frames++;
    }

    r = sd_bus_message_exit_container(m);
    return sd_bus_reply_method_return(m, "");
}

void Client::set_dead() {
    {
        std::lock_guard lock(ipc->clients_mtx);
        auto it = ipc->clients.find(pid);
        if (it == ipc->clients.end())
            return;
    }

    {
        std::lock_guard lock(ipc->dead_clients_mtx);
        active = false;
        ipc->dead_clients.insert(pid);
    }
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
