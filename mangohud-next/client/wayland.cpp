#include "wayland.h"

#include <algorithm>
#include <cstring>
#include <linux/dma-buf.h>
#include <spdlog/spdlog.h>
#include <sys/ioctl.h>
#include <unistd.h>

wl_globals& Wayland::get_global(wl_display* display)
{
    std::lock_guard lock(globals_m);
    auto& global = globals[display];

    if (!global.registry) {
        global.queue = wl_display_create_queue(display);
        auto* wrapped_display = reinterpret_cast<wl_display*>(wl_proxy_create_wrapper(display));
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapped_display), global.queue);

        global.registry = wl_display_get_registry(wrapped_display);
        wl_proxy_wrapper_destroy(wrapped_display);
        wl_registry_add_listener(global.registry, &registry_listener, &global);

        wl_display_roundtrip_queue(display, global.queue);
    }

    return global;
}

bool Wayland::ensure_overlay_data(const std::shared_ptr<surface_data>& surf_data)
{
    if (!surf_data || !surf_data->display || !surf_data->surface)
        return false;

    std::lock_guard lock(surf_data->m);

    auto& globals = get_global(surf_data->display);
    if (!globals.compositor || !globals.subcompositor || !globals.dmabuf)
        return false;
    surf_data->queue = globals.queue;

    if (!surf_data->overlay_surf) {
        surf_data->overlay_surf = wl_compositor_create_surface(globals.compositor);
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(surf_data->overlay_surf), globals.queue);

        wl_region* input_region = wl_compositor_create_region(globals.compositor);
        wl_surface_set_input_region(surf_data->overlay_surf, input_region);
        wl_region_destroy(input_region);

        surf_data->sub_surf = wl_subcompositor_get_subsurface(globals.subcompositor,
                                                              surf_data->overlay_surf,
                                                              surf_data->surface);
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(surf_data->sub_surf), globals.queue);

        if (globals.viewporter)
            surf_data->viewport = wp_viewporter_get_viewport(globals.viewporter, surf_data->overlay_surf);
        if (surf_data->viewport)
            wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(surf_data->viewport), globals.queue);

        if (globals.fractional_scale_manager) {
            surf_data->fractional_scale =
                wp_fractional_scale_manager_v1_get_fractional_scale(
                    globals.fractional_scale_manager, surf_data->surface);
            wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(surf_data->fractional_scale), globals.queue);
            wp_fractional_scale_v1_add_listener(
                surf_data->fractional_scale, &fractional_scale_listener, surf_data.get());
        }

        wl_subsurface_set_position(surf_data->sub_surf, 0, 0);
        wl_subsurface_place_above(surf_data->sub_surf, surf_data->surface);
        wl_subsurface_set_desync(surf_data->sub_surf);
        SPDLOG_DEBUG("wl overlay created: parent=0x{:x}, overlay=0x{:x}, subsurface=0x{:x}",
                     (uint64_t)surf_data->surface, (uint64_t)surf_data->overlay_surf,
                     (uint64_t)surf_data->sub_surf);
    }

    return true;
}

void Wayland::on_registry_global(void* data, wl_registry* registry, uint32_t name,
                                 const char* interface, uint32_t version)
{
    auto& global = *reinterpret_cast<wl_globals*>(data);

    uint32_t compositor_version = std::min(version, 4u);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        global.compositor = reinterpret_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, compositor_version));
        global.compositor_name = name;
    } else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
        global.subcompositor = reinterpret_cast<wl_subcompositor*>(
            wl_registry_bind(registry, name, &wl_subcompositor_interface, compositor_version));
        global.subcompositor_name = name;
    } else if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0) {
        global.dmabuf = reinterpret_cast<zwp_linux_dmabuf_v1*>(
            wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, 3));
        global.dmabuf_name = name;
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        global.viewporter = reinterpret_cast<wp_viewporter*>(
            wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
        global.viewporter_name = name;
    } else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        global.fractional_scale_manager = reinterpret_cast<wp_fractional_scale_manager_v1*>(
            wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1));
        global.fractional_scale_manager_name = name;
    }
}

void Wayland::on_registry_global_remove(void* data, wl_registry*, uint32_t name)
{
    auto& global = *reinterpret_cast<wl_globals*>(data);

    if (name == global.compositor_name) {
        wl_compositor_destroy(global.compositor);
        global.compositor = nullptr;
        global.compositor_name = 0;
    } else if (name == global.subcompositor_name) {
        wl_subcompositor_destroy(global.subcompositor);
        global.subcompositor = nullptr;
        global.subcompositor_name = 0;
    } else if (name == global.viewporter_name) {
        wp_viewporter_destroy(global.viewporter);
        global.viewporter = nullptr;
        global.viewporter_name = 0;
    } else if (name == global.fractional_scale_manager_name) {
        wp_fractional_scale_manager_v1_destroy(global.fractional_scale_manager);
        global.fractional_scale_manager = nullptr;
        global.fractional_scale_manager_name = 0;
    }
}

void Wayland::on_preferred_scale(void* data, wp_fractional_scale_v1*, uint32_t scale)
{
    auto* surf_data = reinterpret_cast<surface_data*>(data);
    if (!surf_data || scale == 0)
        return;

    surf_data->preferred_scale.store(scale, std::memory_order_release);
    SPDLOG_DEBUG("wl fractional scale changed: preferred_scale={}", scale);
}

void Wayland::release_to_server(shm_buffer* buf)
{
    if (!buf->ipc || buf->idx < 0 || !buf->dmabuf_fd)
        return;

    if (!buf->ipc->connected.load(std::memory_order_acquire))
        return;

    dma_buf_export_sync_file sync_file{};
    sync_file.flags = DMA_BUF_SYNC_READ;
    sync_file.fd = -1;

    if (ioctl(buf->dmabuf_fd.get(), DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &sync_file) != 0) {
        SPDLOG_ERROR("DMA_BUF_IOCTL_EXPORT_SYNC_FILE failed: errno={}", errno);
        return;
    }

    if (sync_file.fd < 0) {
        SPDLOG_ERROR("DMA_BUF_IOCTL_EXPORT_SYNC_FILE returned invalid fd");
        return;
    }

    buf->ipc->frame_ready(buf->idx, sync_file.fd);
}

void Wayland::buffer_release(void* data, wl_buffer*)
{
    auto* buf = reinterpret_cast<shm_buffer*>(data);
    if (!buf)
        return;

    buf->busy.store(false, std::memory_order_release);
    release_to_server(buf);
}

void Wayland::update_import(const std::shared_ptr<surface_data>& surf_data)
{
    if (!surf_data || !surf_data->display)
        return;

    std::lock_guard lock(surf_data->m);
    auto& globals = get_global(surf_data->display);
    if (!globals.dmabuf)
        return;

    Fdinfo next_fdinfo;
    uint64_t generation = 0;
    {
        std::lock_guard lock(ipc->m);
        generation = ipc->import_generation.load(std::memory_order_acquire);
        if (generation == imported_generation && !surf_data->buffers.empty())
            return;

        if (ipc->fdinfo.dmabuf_buffer.empty())
            return;

        next_fdinfo = std::move(ipc->fdinfo);
        ipc->fdinfo = {};
    }

    fdinfo = std::move(next_fdinfo);
    if (surf_data->overlay_surf && surf_data->attached) {
        wl_surface_attach(surf_data->overlay_surf, nullptr, 0, 0);
        wl_surface_commit(surf_data->overlay_surf);
        wl_display_flush(surf_data->display);
        wl_display_roundtrip_queue(surf_data->display, globals.queue);
        surf_data->attached = false;
    }
    surf_data->buffers.clear();
    ipc->clear_frames();

    for (uint32_t idx = 0; idx < fdinfo.dmabuf_buffer.size(); idx++) {
        auto& dmabuf = fdinfo.dmabuf_buffer[idx];
        std::shared_ptr<shm_buffer> buf = std::make_shared<shm_buffer>();
        buf->ipc = ipc.get();
        buf->idx = idx;
        buf->dmabuf_fd = unique_fd::adopt(dup(dmabuf.get()));
        buf->width = fdinfo.w;
        buf->height = fdinfo.h;
        buf->stride = fdinfo.stride;
        buf->offset = fdinfo.dmabuf_offset;
        buf->modifier = fdinfo.modifier;
        buf->params = zwp_linux_dmabuf_v1_create_params(globals.dmabuf);
        zwp_linux_buffer_params_v1_add(buf->params, dmabuf.get(), 0, fdinfo.dmabuf_offset,
                                       fdinfo.stride, fdinfo.modifier >> 32,
                                       fdinfo.modifier & 0xffffffff);

        buf->buffer = zwp_linux_buffer_params_v1_create_immed(buf->params, fdinfo.w,
                                                              fdinfo.h,
                                                              fdinfo.fourcc, 0);

        wl_buffer_add_listener(buf->buffer, &Wayland::buffer_listener, buf.get());

        zwp_linux_buffer_params_v1_destroy(buf->params);
        surf_data->buffers.push_back(std::move(buf));
    }

    imported_generation = generation;

    for (auto& buf : surf_data->buffers)
        release_to_server(buf.get());
}

void Wayland::dispatch_events(const std::shared_ptr<surface_data>& surf_data)
{
    if (!surf_data || !surf_data->display)
        return;

    auto& globals = get_global(surf_data->display);
    if (!globals.queue)
        return;

    wl_display_dispatch_queue_pending(surf_data->display, globals.queue);
    wl_display_flush(surf_data->display);
}

std::shared_ptr<shm_buffer> Wayland::present(const std::shared_ptr<surface_data>& surf_data)
{
    if (!surf_data || !surf_data->overlay_surf)
        return nullptr;

    std::lock_guard lock(surf_data->m);

    if (surf_data->buffers.empty())
        return nullptr;

    int idx = ipc->next_frame();
    if (idx < 0 || static_cast<size_t>(idx) >= surf_data->buffers.size())
        return nullptr;

    auto slot = surf_data->buffers[idx];
    if (!slot || !slot->buffer)
        return nullptr;

    bool expected = false;
    if (!slot->busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        SPDLOG_DEBUG("wl dmabuf slot {} is still busy", idx);
        return nullptr;
    }

    wl_surface_attach(surf_data->overlay_surf, slot->buffer, 0, 0);
    if (surf_data->viewport) {
        uint32_t scale = surf_data->preferred_scale.load(std::memory_order_acquire);
        if (scale == 0)
            scale = 120;

        int dst_width = std::max(1, static_cast<int>((uint64_t(slot->width) * 120 + scale - 1) / scale));
        int dst_height = std::max(1, static_cast<int>((uint64_t(slot->height) * 120 + scale - 1) / scale));
        wp_viewport_set_destination(surf_data->viewport, dst_width, dst_height);
    }
    wl_surface_damage(surf_data->overlay_surf, 0, 0, slot->width, slot->height);
    wl_surface_damage_buffer(surf_data->overlay_surf, 0, 0, slot->width, slot->height);
    wl_surface_commit(surf_data->overlay_surf);
    wl_display_flush(surf_data->display);
    surf_data->attached = true;

    return slot;
}

void Wayland::detach(const std::shared_ptr<surface_data>& surf_data, bool wait_for_server)
{
    if (!surf_data)
        return;

    std::lock_guard lock(surf_data->m);
    ipc->clear_frames();
    for (auto& buf : surf_data->buffers) {
        if (buf)
            buf->busy.store(false, std::memory_order_release);
    }

    if (!surf_data->overlay_surf || !surf_data->attached)
        return;

    wl_surface_attach(surf_data->overlay_surf, nullptr, 0, 0);
    wl_surface_commit(surf_data->overlay_surf);
    wl_display_flush(surf_data->display);
    if (wait_for_server) {
        auto& globals = get_global(surf_data->display);
        if (globals.queue)
            wl_display_roundtrip_queue(surf_data->display, globals.queue);
    }
    surf_data->attached = false;
}
