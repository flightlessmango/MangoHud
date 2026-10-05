#include "wayland.h"

#include "../ipc/client.h"
#include "../ipc/ipc.h"
#include "../render/renderer.h"

#include <chrono>
#include <algorithm>
#include <cstring>
#include <cerrno>
#include <poll.h>
#include <pthread.h>
#include <spdlog/spdlog.h>
#include <utility>
#include <xf86drm.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

Wayland::Wayland(IPCServer* ipc_, std::string display_name_)
    : display_name(std::move(display_name_)), ipc(ipc_),
      ctx(wayland_ctx_listener{.data = this, .global = on_registry_global})
{
    display = wl_display_connect(display_name.empty() ? nullptr : display_name.c_str());
    if (!display) {
        SPDLOG_DEBUG("wayland: display unavailable display={}", display_name);
        return;
    }

    auto* globals = ctx.get_global(display);
    if (!globals || !globals->compositor || !layer_shell) {
        SPDLOG_DEBUG("wayland: failed to initialize globals");
        if (layer_shell) {
            zwlr_layer_shell_v1_destroy(layer_shell);
            layer_shell = nullptr;
        }
        wl_display_disconnect(display);
        display = nullptr;
        return;
    }

    surface = wl_create_surface(*globals);
    if (!surface)
        return;

    if (!globals->dmabuf || zwp_linux_dmabuf_v1_get_version(globals->dmabuf) < 4) {
        SPDLOG_DEBUG("wayland: device feedback unavailable display={}", display_name);
        return;
    }

    auto* feedback = zwp_linux_dmabuf_v1_get_surface_feedback(globals->dmabuf, surface);
    if (!feedback)
        return;
    static const zwp_linux_dmabuf_feedback_v1_listener feedback_listener = {
        .done = [](void*, zwp_linux_dmabuf_feedback_v1*) {},
        .format_table = [](void*, zwp_linux_dmabuf_feedback_v1*, int32_t fd, uint32_t) { close(fd); },
        .main_device = [](void* data, zwp_linux_dmabuf_feedback_v1*, wl_array* array) {
            auto* self = static_cast<Wayland*>(data);
            if (array->size != sizeof(dev_t)) {
                SPDLOG_ERROR("wayland: invalid feedback device size={}", array->size);
                return;
            }
            dev_t device_id;
            std::memcpy(&device_id, array->data, sizeof(device_id));
            drmDevicePtr device = nullptr;
            int result = drmGetDeviceFromDevId(device_id, 0, &device);
            if (result < 0) {
                SPDLOG_ERROR("wayland: resolving feedback device failed: {}", strerror(-result));
                return;
            }
            if (device->available_nodes & (1 << DRM_NODE_RENDER)) {
                struct stat node{};
                if (stat(device->nodes[DRM_NODE_RENDER], &node) == 0)
                    self->render_minor = minor(node.st_rdev);
            }
            drmFreeDevice(&device);
        },
        .tranche_done = [](void*, zwp_linux_dmabuf_feedback_v1*) {},
        .tranche_target_device = [](void*, zwp_linux_dmabuf_feedback_v1*, wl_array*) {},
        .tranche_formats = [](void*, zwp_linux_dmabuf_feedback_v1*, wl_array*) {},
        .tranche_flags = [](void*, zwp_linux_dmabuf_feedback_v1*, uint32_t) {},
    };
    int feedback_result = zwp_linux_dmabuf_feedback_v1_add_listener(feedback, &feedback_listener, this);
    if (feedback_result == 0)
        feedback_result = wl_display_roundtrip_queue(display, globals->queue);
    zwp_linux_dmabuf_feedback_v1_destroy(feedback);
    if (feedback_result < 0 || render_minor < 0) {
        SPDLOG_ERROR("wayland: no renderer device from feedback display={}", display_name);
        return;
    }
    SPDLOG_DEBUG("wayland: feedback render minor={} display={}", render_minor, display_name);

    if (globals->viewporter)
        viewport = wp_viewporter_get_viewport(globals->viewporter, surface);
    if (viewport && globals->fractional_scale_manager) {
        fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(
            globals->fractional_scale_manager, surface);
        if (fractional_scale &&
            wp_fractional_scale_v1_add_listener(fractional_scale, &scale_listener, this) != 0) {
            wp_fractional_scale_v1_destroy(fractional_scale);
            fractional_scale = nullptr;
        }
    }

    layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell, surface, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "mangohud");
    if (!layer_surface ||
        zwlr_layer_surface_v1_add_listener(layer_surface, &layer_listener, this) != 0)
        return;

    zwlr_layer_surface_v1_set_anchor(layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_size(layer_surface, 500, 500);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    auto* input = wl_compositor_create_region(globals->compositor);
    wl_surface_set_input_region(surface, input);
    wl_region_destroy(input);
    wl_surface_commit(surface);
    wl_display_flush(display);

    thread = std::thread([this] { run(); });

    SPDLOG_DEBUG("wayland: connected display={}", display_name);
}

Wayland::~Wayland()
{
    quit.store(true, std::memory_order_release);
    if (thread.joinable())
        thread.join();

    std::unique_ptr<ActiveSet> previous;
    {
        std::lock_guard lock(frame_m);
        previous = std::move(active);
    }
    previous.reset();
    retired.clear();

    if (layer_surface)
        zwlr_layer_surface_v1_destroy(layer_surface);
    if (fractional_scale)
        wp_fractional_scale_v1_destroy(fractional_scale);
    if (viewport)
        wp_viewport_destroy(viewport);
    if (surface)
        wl_surface_destroy(surface);
    if (layer_shell)
        zwlr_layer_shell_v1_destroy(layer_shell);

    if (display)
        wl_display_disconnect(display);
}

void Wayland::run()
{
    pthread_setname_np(pthread_self(), "mhud wayland");

    while (!quit.load(std::memory_order_acquire)) {
        dispatch_events();
        if (quit.load(std::memory_order_acquire))
            break;
        std::erase_if(retired, [](const auto& buffer) {
            return !buffer->slot.busy.load(std::memory_order_acquire);
        });

        auto focused = ipc->focused_client(display_name);
        if (!focused) {
            std::lock_guard lock(frame_m);
            if (active)
                focused = active->client.lock();
        }
        if (focused) {
            std::shared_ptr<Renderer::Resources> next;
            {
                std::lock_guard lock(focused->m);
                if (focused->renderer)
                    next = focused->renderer->get_resources();
            }

            bool changed;
            {
                std::lock_guard lock(frame_m);
                changed = !active || active->client.lock() != focused || active->resources != next;
            }
            if (changed && next)
                import_resources(focused);
        }

        present();
        std::this_thread::sleep_for(std::chrono::milliseconds(7));
    }
}

void Wayland::dispatch_events()
{
    if (!display)
        return;

    auto* globals = ctx.get_global(display);
    if (!globals || !globals->queue)
        return;

    if (wl_display_dispatch_queue_pending(display, globals->queue) < 0) {
        SPDLOG_ERROR("wayland: dispatch failed");
        quit.store(true, std::memory_order_release);
        return;
    }

    if (wl_display_prepare_read_queue(display, globals->queue) != 0)
        return;

    wl_display_flush(display);
    pollfd fd{wl_display_get_fd(display), POLLIN, 0};
    const int ready = poll(&fd, 1, 0);
    if (ready > 0 && (fd.revents & POLLIN)) {
        if (wl_display_read_events(display) < 0 ||
            wl_display_dispatch_queue_pending(display, globals->queue) < 0) {
            SPDLOG_ERROR("wayland: reading events failed");
            quit.store(true, std::memory_order_release);
        }
    } else {
        wl_display_cancel_read(display);
        if ((ready < 0 && errno != EINTR) || (fd.revents & (POLLERR | POLLHUP | POLLNVAL)))
            quit.store(true, std::memory_order_release);
    }
}

void Wayland::on_registry_global(void* data, wl_globals&, wl_registry* registry,
                                 uint32_t name, const char* interface, uint32_t version)
{
    auto* self = static_cast<Wayland*>(data);
    if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
        self->layer_shell = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
}

void Wayland::on_layer_configure(void* data, zwlr_layer_surface_v1* surface,
                                 uint32_t serial, uint32_t, uint32_t)
{
    auto* self = static_cast<Wayland*>(data);
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    self->configured = true;
    SPDLOG_DEBUG("wayland: layer surface configured display={}", self->display_name);
}

void Wayland::on_layer_closed(void* data, zwlr_layer_surface_v1*)
{
    auto* self = static_cast<Wayland*>(data);
    self->configured = false;
    self->quit.store(true, std::memory_order_release);
}

void Wayland::on_preferred_scale(void* data, wp_fractional_scale_v1*, uint32_t scale)
{
    if (!scale)
        return;
    auto* self = static_cast<Wayland*>(data);
    self->preferred_scale = scale;
    SPDLOG_DEBUG("wayland: fractional scale changed preferred_scale={}", scale);
}

void Wayland::import_dmabuf(std::shared_ptr<Client> client,
                            const std::vector<BufferSet>&,
                            uint32_t,
                            uint32_t,
                            ExportMethod)
{
    if (!client)
        return;

    std::lock_guard lock(frame_m);
    if (active && active->client.lock() == client)
        active->frames.clear();
}

Wayland::ActiveSet::ActiveSet(Wayland& wayland, const std::shared_ptr<Client>& client)
    : client(client)
{
    std::shared_ptr<Renderer::Resources> next_resources;
    {
        std::lock_guard lock(client->m);
        if (client->renderer)
            next_resources = client->renderer->get_resources();
    }
    if (!next_resources)
        return;

    auto* globals = wayland.ctx.get_global(wayland.display);
    if (!globals || !globals->dmabuf) {
        SPDLOG_DEBUG("wayland: dmabuf global unavailable display={}", wayland.display_name);
        return;
    }

    std::vector<std::shared_ptr<ImportedBuffer>> imported;
    imported.reserve(next_resources->buffers.size());

    for (size_t idx = 0; idx < next_resources->buffers.size(); idx++) {
        const auto& buffer = next_resources->buffers[idx];
        if (!buffer.dmabuf.gbm.fd) {
            SPDLOG_ERROR("wayland: buffer {} has no dmabuf fd", idx);
            return;
        }

        wl_dmabuf_info info{
            .fd = unique_fd::dup(buffer.dmabuf.gbm.fd.get()),
            .width = next_resources->width,
            .height = next_resources->height,
            .stride = buffer.dmabuf.gbm.stride,
            .offset = buffer.dmabuf.gbm.offset,
            .fourcc = buffer.dmabuf.gbm.fourcc,
            .modifier = buffer.dmabuf.gbm.modifier,
        };

        auto next = std::make_shared<ImportedBuffer>();
        next->slot.idx = static_cast<int>(idx);
        next->wayland = &wayland;
        next->client = client;
        next->resources = next_resources;

        if (!wl_import_dmabuf(globals->dmabuf, next->slot, std::move(info))) {
            SPDLOG_ERROR("wayland: failed to import dmabuf display={} pid={} idx={}",
                         wayland.display_name, client->pid, idx);
            return;
        }

        next->slot.dmabuf_fd.reset();
        if (wl_buffer_add_listener(next->slot.buffer, &buffer_listener, next.get()) != 0)
            return;
        imported.push_back(std::move(next));
    }

    resources = next_resources;
    buffers = std::move(imported);
}

bool Wayland::import_resources(const std::shared_ptr<Client>& client)
{
    auto next = std::make_unique<ActiveSet>(*this, client);
    if (!next->resources)
        return false;
    {
        std::lock_guard lock(frame_m);
        active.swap(next);
    }
    if (next) {
        for (const auto& buffer : next->buffers)
            if (buffer->slot.busy.load(std::memory_order_acquire))
                retired.push_back(buffer);
    }
    next.reset();

    for (const auto& buffer : active->buffers) {
        const bool in_use = std::any_of(retired.begin(), retired.end(), [&](const auto& old) {
            return old->resources == buffer->resources && old->slot.idx == buffer->slot.idx &&
                   old->slot.busy.load(std::memory_order_acquire);
        });
        if (!in_use)
            release_buffer(*buffer);
    }

    SPDLOG_DEBUG("wayland: imported dmabufs display={} pid={} buffers={} size={}x{} method={}",
                 display_name,
                 client->pid,
                 active->buffers.size(),
                 active->resources->width,
                 active->resources->height,
                 static_cast<int32_t>(active->resources->method));
    return true;
}

void Wayland::frame_ready(std::shared_ptr<Client> client, int idx, unique_fd fd,
                          std::shared_ptr<Renderer::Resources> resources)
{
    if (!client || idx < 0)
        return;

    {
        std::lock_guard lock(frame_m);
        if (!active || active->client.lock() != client || active->resources != resources)
            return;
        active->frames.push_back({idx, std::move(fd)});
    }

    SPDLOG_TRACE("wayland: frame ready display={} pid={} idx={}",
                 display_name,
                 client->pid,
                 idx);
}

void Wayland::release_buffer(ImportedBuffer& buffer)
{
    auto fd = wl_export_dmabuf_sync_fd(buffer.resources->dmabuf_fd(buffer.slot.idx));
    if (!fd)
        return;

    auto client = buffer.client.lock();
    if (!client)
        return;

    std::lock_guard lock(client->m);
    if (client->renderer)
        client->renderer->frame_ready(buffer.slot.idx, std::move(fd), buffer.resources);
}

void Wayland::present()
{
    if (!configured || quit.load(std::memory_order_acquire))
        return;

    std::shared_ptr<ImportedBuffer> buffer;
    {
        std::lock_guard lock(frame_m);
        if (!active || active->frames.empty())
            return;

        auto& frame = active->frames.front();
        if (frame.idx < 0 || frame.idx >= std::ssize(active->buffers)) {
            SPDLOG_ERROR("wayland: invalid ready frame idx={}", frame.idx);
            active->frames.pop_front();
            return;
        }
        buffer = active->buffers[frame.idx];
        if (buffer->slot.busy.load(std::memory_order_acquire) || !sync_fd_signaled(frame.fd.get()))
            return;

        active->frames.pop_front();
        buffer->slot.busy.store(true, std::memory_order_release);
    }

    uint32_t width = buffer->resources->width;
    uint32_t height = buffer->resources->height;
    if (viewport) {
        width = std::max(1u, static_cast<uint32_t>((uint64_t(width) * 120 + preferred_scale - 1) / preferred_scale));
        height = std::max(1u, static_cast<uint32_t>((uint64_t(height) * 120 + preferred_scale - 1) / preferred_scale));
        wp_viewport_set_destination(viewport, width, height);
    }
    zwlr_layer_surface_v1_set_size(layer_surface, width, height);
    wl_surface_attach(surface, buffer->slot.buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, buffer->resources->width, buffer->resources->height);
    wl_surface_commit(surface);
    wl_display_flush(display);
    SPDLOG_TRACE("wayland: committed frame={} idx={}", frame_seq++, buffer->slot.idx);
}

void Wayland::on_buffer_release(void* data, wl_buffer*)
{
    auto* buffer = static_cast<ImportedBuffer*>(data);
    if (!buffer)
        return;

    buffer->slot.busy.store(false, std::memory_order_release);
    if (!buffer->wayland)
        return;

    buffer->wayland->release_buffer(*buffer);
}
