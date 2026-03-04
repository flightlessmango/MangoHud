#include "client.h"
#include "ipc.h"
#include "server.h"
#include "../render/vulkan_ctx.h"
#include "../render/imgui_ctx.h"

class IPCServer;

void destroy_client_res(clientRes* r) {
    if (!r->vk || !r->device)
        return;

    std::scoped_lock lock(r->m, r->vk->m);
    vkDeviceWaitIdle(r->device);

    for (auto& buf : r->buffer) {
        destroy_vk_images(r->device, buf.dmabuf.image_res);
        destroy_vk_images(r->device, buf.opaque.image_res);
        destroy_vk_images(r->device, buf.source.image_res);
        buf.dmabuf.gbm = {};

        if (buf.sync.fence) {
            vkDestroyFence(r->device, buf.sync.fence, nullptr);
            buf.sync.fence = VK_NULL_HANDLE;
        }

        if (buf.sync.cmd) {
            vkFreeCommandBuffers(r->device, r->cmd_pool, 1, &buf.sync.cmd);
            buf.sync.cmd = VK_NULL_HANDLE;
        }

    }

    if (r->cmd_pool) {
        vkDestroyCommandPool(r->device, r->cmd_pool, nullptr);
        r->cmd_pool = VK_NULL_HANDLE;
    }
}

clientRes::~clientRes() {
    destroy_client_res(this);
}

void clientRes::reinit() {
    destroy_client_res(this);
    reinit_dmabuf = false;
    initialized = false;

    vk->init_client(this);
}

int Client::try_acquire_buffer() {
    std::unique_lock lock(m);
    cv.wait_for(lock, std::chrono::milliseconds(500));

    if (frame_queue.empty())
        return -1;

    ready_frame& frame = frame_queue.front();
    int ret = frame.idx;
    if (sync_fd_blocking(frame.fd.get()))
        frame_queue.pop_front();

    return ret;
}

void Client::setup_handshake(std::string member, sd_bus_slot** slot,
                             sd_bus_message_handler_t callback, std::shared_ptr<Client>& shared) {
    std::string match =
        "type='signal',"
        "path='" + std::string(kObjPath) + "',"
        "interface='" + std::string(kIface) + "',"
        "member='" + member + "'";

    auto* weak = new std::weak_ptr<Client>(shared);
    int r = sd_bus_add_match(bus, slot, match.c_str(), callback, weak);
    if (r < 0) {
        SPDLOG_ERROR("match {} {} ({}) ObjPath: {} Iface: {}", member, r, strerror(-r), kObjPath, kIface);
        delete weak;
        *slot = nullptr;
        return;
    } else {
        sd_bus_slot_set_destroy_callback(*slot, [](void* p) {
            delete static_cast<std::weak_ptr<Client>*>(p);
        });
    }
}

void Client::init(std::shared_ptr<Client>& shared) {
    if (!shared) {
        set_dead();
        return;
    }

    self_weak = shared;

    setup_handshake("on_connect", &handshake_slot, &Client::on_connect, shared);
    setup_handshake("frame_samples", &frame_samples_slot, &Client::frame_samples, shared);
    setup_handshake("spdlog", &spdlog_slot, &Client::spdlog_msg, shared);
    setup_handshake("frame_ready", &frame_slot, &Client::on_frame, shared);

    stop_eventfd = eventfd(0, EFD_CLOEXEC);
    if (stop_eventfd < 0) { set_dead(); return; }

    work_eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (work_eventfd < 0) { set_dead(); return; }

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

    if (work_eventfd < 0) {
        SPDLOG_ERROR("eventfd(work) failed ({})", strerror(errno));
        set_dead();
        return;
    }

    stop_src = nullptr;
    auto* stop_weak = new std::weak_ptr<Client>(shared);
    r = sd_event_add_io(event, &stop_src, stop_eventfd, EPOLLIN, &Client::on_stop_event, stop_weak);
    if (r < 0) {
        SPDLOG_ERROR("sd_event_add_io(stop_eventfd) {} ({})", r, strerror(-r));
        sd_event_unref(event);
        delete stop_weak;
        event = nullptr;
        set_dead();
        return;
    } else {
        sd_event_source_set_destroy_callback(stop_src, [](void* p) {
            delete static_cast<std::weak_ptr<Client>*>(p);
        });
    }

    work_src = nullptr;
    auto* work_weak = new std::weak_ptr<Client>(shared);
    r = sd_event_add_io(event, &work_src, work_eventfd, EPOLLIN, &Client::on_work_event, work_weak);
    if (r < 0) {
        sd_event_unref(event);
        event = nullptr;
        delete work_weak;
        SPDLOG_ERROR("sd_event_add_io(work_eventfd) {} ({})", r, strerror(-r));
        set_dead();
        return;
    } else {
        sd_event_source_set_destroy_callback(work_src, [](void* p) {
            delete static_cast<std::weak_ptr<Client>*>(p);
        });
    }

    thread = std::thread([self = shared] { self->dbus_thread(); });
    run_t  = std::thread([self = shared] { self->run(); });
}

int Client::on_stop_event(sd_event_source *s, int fd, uint32_t revents, void *userdata) {
    (void)revents;
    (void)userdata;

    uint64_t v = 0;
    for (;;) {
        ssize_t n = read(fd, &v, sizeof(v));
        if (n > 0) {
            break;
        }
        if (n == -1 && errno == EINTR) {
            continue;
        }
        break;
    }

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
    pthread_setname_np(pthread_self(), ("c_dbus " + std::to_string(pid)).substr(0, 15).c_str());
    send_config();

    int r = sd_event_loop(event);
    if (r < 0 && !stop.load())
        SPDLOG_ERROR("sd_event_loop {} ({})", r, strerror(-r));

    SPDLOG_DEBUG("dbus thread exited");
}

int Client::on_connect(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
    auto* w = static_cast<std::weak_ptr<Client>*>(userdata);
    auto self = w->lock();
    if (!self)
        return 0;

    std::lock_guard lock(self->resources->m);

    const char* engine = "";
    int r = sd_bus_message_read(m, "sxi", &engine, &self->renderMinor, &self->buffer_size);
    if (r < 0) {
        SPDLOG_ERROR("on_connect read(sxi) {} ({})", r, strerror(-r));
        self->set_dead();
        return 0;
    }
    self->pEngineName = engine;

    if (!self->resources->vk) self->resources->vk = self->server->vk();

    return 0;
}

void Client::send_dmabuf(){
    Fdinfo fdinfo;
    {
        std::lock_guard lock(resources->m);
        resources->send_dmabuf = false;
        for (auto& buf : resources->buffer) {
            unique_fd dmabuf = unique_fd::dup(buf.dmabuf.gbm.fd);
            unique_fd opaque = unique_fd::dup(buf.opaque.fd);
            if (!dmabuf || !opaque) {
                SPDLOG_ERROR("send_dmabuf: failed to dup an fd. critical error");
                return;
            }
            fdinfo.dmabuf_buffer.push_back(std::move(dmabuf));
            fdinfo.opaque_buffer.push_back(std::move(opaque));
        }
        fdinfo.w = resources->w;
        fdinfo.h = resources->h;
        fdinfo.server_render_minor = resources->server_render_minor;
        fdinfo.modifier = resources->buffer.back().dmabuf.gbm.modifier;
        fdinfo.dmabuf_offset = resources->buffer.back().dmabuf.gbm.offset;
        fdinfo.stride = resources->buffer.back().dmabuf.gbm.stride;
        fdinfo.fourcc = resources->buffer.back().dmabuf.gbm.fourcc;
        fdinfo.plane_size = resources->buffer.back().dmabuf.gbm.plane_size;
        fdinfo.opaque_size = resources->buffer.back().opaque.size;
        fdinfo.opaque_offset = resources->buffer.back().opaque.offset;
    }

    auto weak = self_weak;
    post([weak, fdinfo = std::move(fdinfo)]() {
        auto self = weak.lock();
        if (!self)
            return 0;
        SPDLOG_DEBUG("sending dmabuf");
        sd_bus_message* msg = nullptr;
        int ret = sd_bus_message_new_signal(self->bus, &msg, kObjPath, kIface, "dmabuf");

        if (ret < 0) {
            SPDLOG_DEBUG("failed to set signal {}", ret);
            return ret;
        }

        bool has_dmabuf = true;
        ret = sd_bus_message_append(
            msg,
            "tuuutttuuxb",
            fdinfo.modifier,
            fdinfo.dmabuf_offset,
            fdinfo.stride,
            fdinfo.fourcc,
            fdinfo.plane_size,
            fdinfo.opaque_size,
            fdinfo.opaque_offset,
            fdinfo.w,
            fdinfo.h,
            fdinfo.server_render_minor,
            has_dmabuf
        );

        if (ret < 0) {
            SPDLOG_DEBUG("failed to append to message {} ({})", ret, strerror(-ret));
            sd_bus_message_unref(msg);
            return ret;
        }

        ret = sd_bus_message_open_container(msg, 'a', "(hh)");
        if (ret < 0) {
            SPDLOG_DEBUG("sd_bus_message_open_container array {} ({})", ret, strerror(-ret));
            sd_bus_message_unref(msg);
            return ret;
        }

        if (has_dmabuf) {
            for (size_t i = 0; i < fdinfo.dmabuf_buffer.size(); i++) {
                ret = sd_bus_message_open_container(msg, 'r', "hh");
                if (ret < 0) {
                    SPDLOG_DEBUG("sd_bus_message_open_container struct {} ({})", ret, strerror(-ret));
                    break;
                }

                ret = sd_bus_message_append(
                    msg,
                    "hh",
                    fdinfo.dmabuf_buffer[i].get(),
                    fdinfo.opaque_buffer[i].get()
                );

                if (ret < 0) {
                    SPDLOG_DEBUG("sd_bus_message_append fds {} ({})", ret, strerror(-ret));
                    int cr = sd_bus_message_close_container(msg);
                    if (cr < 0) {
                        SPDLOG_DEBUG("sd_bus_message_close_container struct {} ({})", cr, strerror(-cr));
                    }
                    break;
                }

                ret = sd_bus_message_close_container(msg);
                if (ret < 0) {
                    SPDLOG_DEBUG("sd_bus_message_close_container struct {} ({})", ret, strerror(-ret));
                    break;
                }
            }
        }

        ret = sd_bus_message_close_container(msg);
        if (ret < 0) {
            sd_bus_message_unref(msg);
            return ret;
        }

        ret = sd_bus_send(self->bus, msg, nullptr);
        sd_bus_message_unref(msg);
        if (ret < 0) {
            self->set_dead();
            SPDLOG_DEBUG("sd_bus_send {} ({})", ret, strerror(-ret));
            return ret;
        }

        return 0;
    });
}

void Client::send_config() {
    const double fps_limit = server->config->get<double>("fps_limit");
    auto weak = self_weak;
    post([weak, fps_limit]() {
        auto self = weak.lock();
        if (!self)
            return 0;
        sd_bus_message* msg = nullptr;

        int r = sd_bus_message_new_signal(self->bus, &msg, kObjPath, kIface, "config");

        if (r < 0) {
            SPDLOG_DEBUG("failed to set signal {}", r);
            return r;
        }

        r = sd_bus_message_append(msg, "d", fps_limit);
        SPDLOG_DEBUG("sent fps_limit: {}", fps_limit);

        if (r < 0) {
            SPDLOG_DEBUG("failed to append to message {}", r);
            sd_bus_message_unref(msg);
            return r;
        }

        r = sd_bus_send(self->bus, msg, nullptr);
        sd_bus_message_unref(msg);
        if (r < 0) {
            self->set_dead();
            SPDLOG_DEBUG("failed to send message {}", r);
            return r;
        }
        return 0;
    });
}

int Client::frame_samples(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
    auto* w = static_cast<std::weak_ptr<Client>*>(userdata);
    auto self = w->lock();
    if (!self)
        return 0;
    int r = sd_bus_message_enter_container(m, 'a', "(tt)");

    std::lock_guard client_lock(self->m);
    for (;;) {
        uint64_t seq = 0, t_ns = 0;
        r = sd_bus_message_read(m, "(tt)", &seq, &t_ns);
        if (r < 0) return r;
        if (r == 0) break;
        std::lock_guard lock(self->samples_m);
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
    active.store(false);
}

int Client::on_work_event(sd_event_source *s, int fd, uint32_t revents, void *userdata) {
    auto* w = static_cast<std::weak_ptr<Client>*>(userdata);
    auto self = w->lock();
    if (!self)
        return 0;
    if (!self->active.load())
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

void Client::run() {
    pthread_setname_np(pthread_self(), ("c_run-" + std::to_string(pid)).substr(0, 15).c_str());
    int buf_idx = -1;
    while (!stop.load()) {
        while (!resources->vk) {
            sleep(1);
            continue;
        }
        if (!resources->initialized) resources->vk->init_client(resources.get(), buffer_size);

        if (!resources->table) return;
        if (resources->send_dmabuf) send_dmabuf();
        if (buf_idx == -1)
            buf_idx = try_acquire_buffer();

        if (buf_idx >= 0) {
            auto& buf = resources->buffer[buf_idx];
            if (!resources->vk->imgui->draw(resources, buf))
                continue;

            resources->vk->submit(resources, buf_idx);
            frame_ready(buf_idx);
            buf_idx = -1;
        }
    }
}

Client::~Client() {
    {
        std::lock_guard lock(samples_m);
        samples.clear();
        frametimes.clear();
    }

    sd_bus_slot_unref(handshake_slot);
    sd_bus_slot_unref(frame_samples_slot);
    sd_bus_slot_unref(spdlog_slot);
    sd_bus_slot_unref(frame_slot);

    destroy_client_res(resources.get());

    sd_event_source_disable_unref(stop_src);
    stop_src = nullptr;

    sd_event_source_disable_unref(work_src);
    work_src = nullptr;

    sd_bus_detach_event(bus);

    sd_event_unref(event);
    event = nullptr;

    if (bus) {
        sd_bus_unref(bus);
        bus = nullptr;
    }

    if (work_eventfd >= 0) {
        close(work_eventfd);
        work_eventfd = -1;
    }

    if (stop_eventfd >= 0) {
        close(stop_eventfd);
        stop_eventfd = -1;
    }
}

void Client::stop_and_join() {
    {
        std::lock_guard lock(m);
        stop.store(true, std::memory_order_release);
    }
    cv.notify_all();
    uint64_t one = 1;
    for (;;) {
        ssize_t n = write(stop_eventfd, &one, sizeof(one));
        if (n == (ssize_t)sizeof(one)) {
            break;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        break;
    }

    if (thread.joinable()) {
        thread.join();
    }
    if (run_t.joinable()) {
        run_t.join();
    }
}

int Client::spdlog_msg(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* w = static_cast<std::weak_ptr<Client>*>(userdata);
    auto self = w->lock();
    if (!self)
        return 0;

    int level;
    const char* f = "";
    int line;
    const char* t = "";
    int r = sd_bus_message_read(m, "isis", &level, &f, &line, &t);
    std::string file = f;
    std::string text = t;

    if (r < 0)
        return r;

    if (!self->logger) {
        auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        //TODO this should probably be a unique name in the future
        auto name = std::string("\x1b[38;2;173;100;193m") + "MANGOHUD" + "\x1b[0m" +
                    " " +
                    "\x1b[38;2;46;151;98m" + std::string("CLIENT") + "\x1b[0m";
        self->logger = std::make_shared<spdlog::logger>(name, sink);
        self->logger->set_level(spdlog::level::debug);
        self->logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v");
    }

    spdlog::source_loc loc{file.c_str(), line, ""};
    auto lvl = static_cast<spdlog::level::level_enum>(level);
    self->logger->log(loc, lvl, "{}", text);
    return 0;
}

void Client::frame_ready(uint32_t idx) {
    auto fence = resources->buffer[idx].sync.fence;
    auto fd = unique_fd::adopt(resources->vk->get_fence_fd(fence));
    auto weak = self_weak;
    post([weak, idx, fd = std::move(fd)]() {
        auto self = weak.lock();
        if (!self)
            return 0;

        int r = sd_bus_emit_signal(self->bus, kObjPath, kIface, "frame_ready", "uh", idx, fd.get());
        if (r < 0) {
            SPDLOG_ERROR("sd_bus_emit_signal {} ({})", r, strerror(-r));
            return r;
        }

        return 0;
    });
}

int Client::on_frame(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* w = static_cast<std::weak_ptr<Client>*>(userdata);
    auto self = w->lock();

    ready_frame frame;
    int fd;
    int r = sd_bus_message_read(m, "uh", &frame.idx, &fd);
    if (r < 0) {
        SPDLOG_ERROR("sd_bus_call_method {} ({})", r, strerror(-r));
        return r;
    }

    frame.fd = unique_fd::dup(fd);
    std::lock_guard lock(self->frame_m);
    self->frame_queue.push_back(std::move(frame));
    self->cv.notify_all();
    return 0;
}
