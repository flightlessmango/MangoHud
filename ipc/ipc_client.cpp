#include "ipc.h"
#include "ipc_client.h"
#include "../client/layer.h"

IPCClient::IPCClient(Layer* layer_) : layer(layer_) {
    auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto spdlog_sink = std::make_shared<spdlogSink>(this);
    logger = std::make_shared<spdlog::logger>("MANGOHUD", spdlog::sinks_init_list{console, spdlog_sink});
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::level_enum::debug);
    SPDLOG_DEBUG("init dbus client");
    wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    work_eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

void IPCClient::start(int64_t renderMinor_, std::string& pEngineName_,
                      int image_count, std::shared_ptr<IPCClient> shared) {
    if (thread.joinable())
        return;

    renderMinor = renderMinor_;
    pEngineName = pEngineName_;
    buffer_size = image_count;
    thread = std::thread([self = shared] { self->bus_thread(); });
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
    bool has_gbm = false;
    Fdinfo fdinfo{};

    int r = sd_bus_message_read(
        m,
        "tuuutttuuxb",
        &fdinfo.modifier,
        &fdinfo.dmabuf_offset,
        &fdinfo.stride,
        &fdinfo.fourcc,
        &fdinfo.plane_size,
        &fdinfo.opaque_size,
        &fdinfo.opaque_offset,
        &fdinfo.w,
        &fdinfo.h,
        &fdinfo.server_render_minor,
        &has_gbm
    );

    if (r < 0) {
        SPDLOG_DEBUG("sd_bus_message_read header {} ({})", r, strerror(-r));
        return r;
    }

    r = sd_bus_message_enter_container(m, 'a', "(hh)");
    if (r < 0) {
        SPDLOG_DEBUG("sd_bus_message_enter_container array {} ({})", r, strerror(-r));
        return r;
    }

    for (;;) {
        int gbm_fd = -1;
        int opaque_fd = -1;
        int sema_fd = -1;

        r = sd_bus_message_read(m, "(hh)", &gbm_fd, &opaque_fd, &sema_fd);
        if (r < 0) {
            SPDLOG_DEBUG("sd_bus_message_read (hh) {} ({})", r, strerror(-r));
            sd_bus_message_exit_container(m);
            return r;
        }
        if (r == 0) {
            break;
        }

        unique_fd d = unique_fd::dup(gbm_fd);
        unique_fd o = unique_fd::dup(opaque_fd);
        if (!d || !o) {
            SPDLOG_ERROR("on_dmabuf dup failed");
            return 0;
        }

        fdinfo.dmabuf_buffer.push_back(std::move(d));
        fdinfo.opaque_buffer.push_back(std::move(o));
    }

    r = sd_bus_message_exit_container(m);
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_message_exit_container array {} ({})", r, strerror(-r));
        return r;
    }

    {
        if (self->layer)
            self->layer->overlay_vk->init_dmabufs(fdinfo);

        std::lock_guard lock(self->m);
        self->fdinfo = std::move(fdinfo);
        self->needs_import.store(true);
    }

    return 0;
}

void IPCClient::disconnect_bus() {
    if (work_src) {
        sd_event_source_unref(work_src);
        work_src = nullptr;
    }

    if (dmabuf_slot) {
        sd_bus_slot_unref(dmabuf_slot);
        dmabuf_slot = nullptr;
    }
    if (config_slot) {
        sd_bus_slot_unref(config_slot);
        config_slot = nullptr;
    }
    if (frame_slot) {
        sd_bus_slot_unref(frame_slot);
        frame_slot = nullptr;
    }

    if (bus) {
        sd_bus_flush_close_unref(bus);
        bus = nullptr;
    }

    if (event) {
        sd_event_unref(event);
        event = nullptr;
    }
}

bool IPCClient::connect_bus() {
    disconnect_bus();
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

    std::string match_frame =
        "type='signal',"
        "path='" + std::string(kObjPath) + "',"
        "interface='" + std::string(kIface) + "',"
        "member='frame_ready'";

    r = sd_bus_add_match(b, &frame_slot, match_frame.c_str(), &IPCClient::on_frame, this);
    if (r < 0) {
        SPDLOG_ERROR("frame config: {} ({})", r, strerror(-r));
        sd_bus_unref(b);
        return false;
    }

    r = sd_event_new(&event);
    if (r < 0) {
        SPDLOG_ERROR("sd_event_new {} ({})", r, strerror(-r));
        return false;
    }

    r = sd_bus_attach_event(b, event, 0);
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_attach_event {} ({})", r, strerror(-r));
        sd_event_unref(event);
        event = nullptr;
        return false;
    }

    r = sd_event_add_io(event, &work_src, work_eventfd, EPOLLIN, &IPCClient::on_work_event, this);
    if (r < 0) {
        sd_event_unref(event);
        event = nullptr;
        SPDLOG_ERROR("sd_event_add_io(work_eventfd) {} ({})", r, strerror(-r));
        return false;
    }

    bus = b;
    return true;
}

void IPCClient::run_bus() {
    int r = sd_event_loop(event);
    if (r < 0 && !quit.load())
        SPDLOG_ERROR("sd_event_loop {} ({})", r, strerror(-r));

    sd_event_unref(event);
}

void IPCClient::bus_thread() {
    pthread_setname_np(pthread_self(), "mangohud_dbus");
    quit.store(false);
    while (!quit.load()) {
        connected.store(false);
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
    stop_wait.store(true);
    sd_event_exit(event, 0);
    if (thread.joinable()) thread.join();
}

bool IPCClient::on_connect() {
    post([this] {
        int r = 0;
        sd_bus_message *m = nullptr;
        r = sd_bus_message_new_signal(bus, &m, kObjPath, kIface, "on_connect");
        if (r < 0) {
            SPDLOG_ERROR("sd_bus_message_new_signal {} ({})", r, strerror(-r));
            return false;
        }

        r = sd_bus_message_append(
            m,
            "sxi",
            pEngineName.c_str(),
            (int64_t)renderMinor,
            buffer_size
        );

        r = sd_bus_send(bus, m, nullptr);
        sd_bus_message_unref(m);

        if (r < 0) {
            SPDLOG_ERROR("sd_bus_send {} ({})", r, strerror(-r));
            return false;
        }

        std::lock_guard lock(sync_mtx);
        for (int i = 0; i < buffer_size; i++)
            frame_queue.push_back({i, unique_fd::adopt(-1)});

        return true;
    });

    connected.store(true);
    return true;
}

int IPCClient::push_queue() {
    std::deque<Sample> out;
    {
        std::lock_guard<std::mutex> lock(samples_mtx);
        if (samples.empty() || !connected.load())
            return 0;

        out.swap(samples);
    }

    post([this, out = std::move(out)] {
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
            sd_event_exit(event, 0);
            return r;
        }

        sd_bus_message_unref(m);
        return 0;
    });

    return 0;
}

int IPCClient::request_fd_from_server() {
    sd_bus *bus_ = nullptr;
    int r = sd_bus_open_user(&bus_);
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_open_user {} ({})", r, strerror(-r));
        return r;
    }

    sd_bus_message *m = nullptr;
    sd_bus_message *reply = nullptr;
    r = sd_bus_message_new_method_call(bus_, &m, kBusName, kObjPath, kIface, "request_fd");
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_message_new_method_call: {} ({})", r, strerror(-r));
        return -1;
    }

    static constexpr uint64_t kTimeoutUsec = 2ULL * 1000ULL * 1000ULL; // 2 s
    r = sd_bus_call(bus_, m, kTimeoutUsec, nullptr, &reply);

    if (r < 0) {
        SPDLOG_ERROR("sd_bus_call {} ({})", r, strerror(-r));
        sd_bus_message_unref(reply);
        sd_bus_message_unref(m);
        sd_bus_unref(bus_);
        return -1;
    }

    int fd_msg = -1;
    r = sd_bus_message_read(reply, "h", &fd_msg);
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_message_read {} ({})", r, strerror(-r));
        sd_bus_message_unref(reply);
        sd_bus_message_unref(m);
        sd_bus_unref(bus_);
        return -1;
    }

    int fd_copy = dup(fd_msg);
    if (fd_copy < 0) {
        SPDLOG_ERROR("dup failed {} ({})", errno, strerror(errno));
        sd_bus_message_unref(reply);
        sd_bus_message_unref(m);
        sd_bus_unref(bus_);
        return -1;
    }

    sd_bus_message_unref(reply);
    sd_bus_message_unref(m);
    sd_bus_unref(bus_);

    return fd_copy;
}

void IPCClient::send_spdlog(const int level, const char* file, const int line, const std::string& text) {
    if (!bus)
        return;

    post([this, level, file, line, text] {
        int r = 0;
        {
            std::lock_guard<std::mutex> lock(bus_mtx);
            r = sd_bus_emit_signal(
                bus,
                kObjPath,
                kIface,
                "spdlog",
                "isis",
                level,
                file,
                line,
                text.c_str()
            );
        }

        if (r < 0) {
            sd_event_exit(event, 0);
            return;
        }
    });
}

int IPCClient::on_work_event(sd_event_source *s, int fd, uint32_t revents, void *userdata) {
    auto* self = static_cast<IPCClient*>(userdata);
    if (!self)
        return 0;

    if (self->quit.load())
        return 0;

    uint64_t v = 0;
    while (read(fd, &v, sizeof(v)) == sizeof(v)) {}

    for (;;) {
        std::packaged_task<void()> task;
        {
            std::lock_guard<std::mutex> lock(self->work_mtx);
            if (self->work_q.empty())
                return 0;

            task = std::move(self->work_q.front());
            self->work_q.pop();
        }
        task();
    }

    return 0;
}

void IPCClient::frame_ready(uint32_t idx, int f) {
    auto fd = unique_fd::adopt(f);
    post([this, idx, fd = std::move(fd)]() {
        int r = sd_bus_emit_signal(bus, kObjPath, kIface, "frame_ready", "uh", idx, fd.get());

        if (r < 0) {
            SPDLOG_ERROR("sd_bus_emit_signal {} ({}) ObjPath: {} Iface: {}",
                         r, strerror(-r), kObjPath, kIface);
            throw std::runtime_error("sd_bus_emit_signal");
        }
    });
}

int IPCClient::on_frame(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* self = static_cast<IPCClient*>(userdata);

    ready_frame frame;
    int fd;
    int r = sd_bus_message_read(m, "uh", &frame.idx, &fd);
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_message_read {} ({})", r, strerror(-r));
        if (fd >= 0)
            close(fd);
        return r;
    }

    frame.fd = unique_fd::dup(fd);
    std::lock_guard lock(self->sync_mtx);
    self->frame_queue.push_back(std::move(frame));
    return 0;
}
