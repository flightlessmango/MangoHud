#include "wayland.h"

#include <algorithm>
#include <cstring>
#include <linux/dma-buf.h>
#include <spdlog/spdlog.h>
#include <sys/ioctl.h>
#include <unistd.h>

Wayland::~Wayland()
{
    quit.store(true);
    if (thread.joinable())
        thread.join();

    destroy_wayland_objects();
}

bool Wayland::request_presentation_feedback(const std::shared_ptr<surface_data>& surf_data, wl_globals& globals,
                                            wl_surface* surface, SampleType type, const char* surface_name)
{
    if (!surf_data || !surface || !globals.presentation)
        return false;

    auto feedback_state = surf_data->presentation_feedback;
    if (feedback_state->pending(type).exchange(true, std::memory_order_acq_rel))
        return false;

    auto* feedback = wp_presentation_feedback(globals.presentation, surface);
    if (!feedback) {
        feedback_state->pending(type).store(false, std::memory_order_release);
        return false;
    }

    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(feedback), globals.queue);
    auto* feedback_data = new presentation_feedback_data{ipc, feedback_state, type, surface_name, this};
    if (wp_presentation_feedback_add_listener(feedback, &presentation_feedback_listener, feedback_data) != 0) {
        wp_presentation_feedback_destroy(feedback);
        delete feedback_data;
        feedback_state->pending(type).store(false, std::memory_order_release);
        return false;
    }

    return true;
}

bool Wayland::ensure_overlay_data(const std::shared_ptr<surface_data>& surf_data)
{
    if (!surf_data || !surf_data->display || !surf_data->surface)
        return false;

    std::lock_guard lock(surf_data->m);

    auto* globals = ctx.get_global(surf_data->display);
    if (!globals)
        return false;

    if (request_presentation_feedback(surf_data, *globals, surf_data->surface, SampleType::Output, app_surface_name))
        wl_display_flush(surf_data->display);

    if (!globals->compositor || !globals->subcompositor || !globals->dmabuf)
        return false;
    surf_data->queue = globals->queue;

    if (!surf_data->overlay_surf) {
        surf_data->overlay_surf = wl_compositor_create_surface(globals->compositor);
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(surf_data->overlay_surf), globals->queue);

        wl_region* input_region = wl_compositor_create_region(globals->compositor);
        wl_surface_set_input_region(surf_data->overlay_surf, input_region);
        wl_region_destroy(input_region);

        surf_data->sub_surf = wl_subcompositor_get_subsurface(globals->subcompositor,
                                                              surf_data->overlay_surf,
                                                              surf_data->surface);
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(surf_data->sub_surf), globals->queue);

        if (globals->viewporter)
            surf_data->viewport = wp_viewporter_get_viewport(globals->viewporter, surf_data->overlay_surf);
        if (surf_data->viewport)
            wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(surf_data->viewport), globals->queue);

        if (globals->fractional_scale_manager) {
            surf_data->fractional_scale =
                wp_fractional_scale_manager_v1_get_fractional_scale(
                    globals->fractional_scale_manager, surf_data->surface);
            wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(surf_data->fractional_scale), globals->queue);
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

void Wayland::on_global(void* data, wl_globals& global, wl_registry* registry,
                        uint32_t name, const char* interface, uint32_t version)
{
    if (strcmp(interface, wl_seat_interface.name) != 0)
        return;

    auto* wayland = static_cast<Wayland*>(data);
    if (!wayland)
        return;

    auto seat = std::make_unique<seat_data>();
    seat->seat = reinterpret_cast<wl_seat*>(
        wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5u)));
    if (!seat->seat)
        return;

    seat->queue = global.queue;
    seat->display = global.display;
    seat->registry_name = name;
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(seat->seat), global.queue);
    if (wl_seat_add_listener(seat->seat, &seat_listener, seat.get()) != 0) {
        seat->destroy();
        return;
    }

    std::lock_guard lock(wayland->seats_m);
    wayland->seats.push_back(std::move(seat));
}

void Wayland::on_global_remove(void* data, wl_globals& global, uint32_t name)
{
    auto* wayland = static_cast<Wayland*>(data);
    if (wayland)
        wayland->remove_seat(name, global.display);
}

void Wayland::on_seat_capabilities(void* data, wl_seat* wl_seat, uint32_t capabilities)
{
    auto* seat = static_cast<seat_data*>(data);
    if (!seat || !wl_seat)
        return;

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        if (seat->keyboard)
            return;

        seat->keyboard = wl_seat_get_keyboard(wl_seat);
        if (!seat->keyboard)
            return;

        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(seat->keyboard), seat->queue);
        if (wl_keyboard_add_listener(seat->keyboard, &keyboard_listener, seat) != 0)
            seat->release_keyboard();
    } else if (seat->keyboard) {
        auto snapshot = seat->set_keyboard(false, os_time_get_nano());
        SPDLOG_DEBUG("wl focus signal: keyboard=false keyboard_entered={} pointer_entered={} presentation={} focused={} seat={}",
                     snapshot.focus.keyboard.active,
                     snapshot.focus.pointer.active,
                     snapshot.focus.presentation.active,
                     snapshot.focus.focused(),
                     snapshot.identifier);
        seat->release_keyboard();
    }
}

void Wayland::on_seat_name(void* data, wl_seat*, const char* name)
{
    auto* seat = static_cast<seat_data*>(data);
    if (!seat || !name)
        return;

    auto snapshot = seat->set_name(name);

    SPDLOG_DEBUG("wl seat name: registry_name={} seat={}", seat->registry_name, snapshot.identifier);
}

void Wayland::on_keyboard_keymap(void*, wl_keyboard*, uint32_t, int32_t fd, uint32_t)
{
    if (fd >= 0)
        close(fd);
}

void Wayland::on_keyboard_enter(void* data, wl_keyboard*, uint32_t serial, wl_surface* surface, wl_array*)
{
    auto* seat = static_cast<seat_data*>(data);
    SPDLOG_DEBUG("wl keyboard enter: seat={} serial={} surface=0x{:x}",
                 seat ? seat->identifier() : "unknown", serial, (uint64_t)surface);
    if (seat) {
        auto snapshot = seat->set_keyboard(true, os_time_get_nano());
        SPDLOG_DEBUG("wl focus signal: keyboard=true keyboard_entered={} pointer_entered={} presentation={} focused={} seat={}",
                     snapshot.focus.keyboard.active,
                     snapshot.focus.pointer.active,
                     snapshot.focus.presentation.active,
                     snapshot.focus.focused(),
                     snapshot.identifier);
    }
}

void Wayland::on_keyboard_leave(void* data, wl_keyboard*, uint32_t serial, wl_surface* surface)
{
    auto* seat = static_cast<seat_data*>(data);
    SPDLOG_DEBUG("wl keyboard leave: seat={} serial={} surface=0x{:x}",
                 seat ? seat->identifier() : "unknown", serial, (uint64_t)surface);
    if (seat) {
        auto snapshot = seat->set_keyboard(false, os_time_get_nano());
        SPDLOG_DEBUG("wl focus signal: keyboard=false keyboard_entered={} pointer_entered={} presentation={} focused={} seat={}",
                     snapshot.focus.keyboard.active,
                     snapshot.focus.pointer.active,
                     snapshot.focus.presentation.active,
                     snapshot.focus.focused(),
                     snapshot.identifier);
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

void Wayland::on_presentation_feedback_presented(void* data, struct wp_presentation_feedback* feedback,
                                                uint32_t tv_sec_hi, uint32_t tv_sec_lo,
                                                uint32_t tv_nsec, uint32_t refresh,
                                                uint32_t seq_hi, uint32_t seq_lo,
                                                uint32_t flags)
{
    auto* feedback_data = static_cast<presentation_feedback_data*>(data);
    auto* surface = feedback_data && feedback_data->surface ? feedback_data->surface : "unknown";
    uint64_t tv_sec = (uint64_t(tv_sec_hi) << 32) | tv_sec_lo;
    uint64_t seq = (uint64_t(seq_hi) << 32) | seq_lo;
    uint64_t presented_ns = tv_sec * 1000000000ULL + tv_nsec;
    if (feedback_data) {
        uint64_t sample_seq = seq;
        if (feedback_data->type == SampleType::Output)
            sample_seq = refresh > 0 ? presented_ns / refresh : seq;
        else if (feedback_data->type == SampleType::Hud && feedback_data->feedback_state)
            sample_seq = feedback_data->feedback_state->next_hud_seq();

        if (auto ipc = feedback_data->ipc.lock())
            ipc->add_to_queue(feedback_data->type, sample_seq, presented_ns);

        if (feedback_data->type == SampleType::Output && feedback_data->wayland)
            feedback_data->wayland->set_presentation_focus(true, os_time_get_nano());
    }

    SPDLOG_TRACE("wl presentation feedback: {} presented={}.{:09} refresh={} seq={} flags=0x{:x}",
                 surface, tv_sec, tv_nsec, refresh, seq, flags);

    if (feedback_data)
        feedback_data->clear_pending();
    wp_presentation_feedback_destroy(feedback);
    delete feedback_data;
}

void Wayland::on_presentation_feedback_discarded(void* data, struct wp_presentation_feedback* feedback)
{
    auto* feedback_data = static_cast<presentation_feedback_data*>(data);
    auto* surface = feedback_data && feedback_data->surface ? feedback_data->surface : "unknown";
    SPDLOG_TRACE("wl presentation feedback: {} discarded", surface);
    if (feedback_data && feedback_data->type == SampleType::Output && feedback_data->wayland)
        feedback_data->wayland->set_presentation_focus(false, os_time_get_nano());
    if (feedback_data)
        feedback_data->clear_pending();
    wp_presentation_feedback_destroy(feedback);
    delete feedback_data;
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

void Wayland::destroy_wayland_objects()
{
    std::vector<std::shared_ptr<surface_data>> surface_list;
    {
        std::lock_guard lock(surf_m);
        for (auto& [_, surf_data] : surfaces) {
            if (surf_data)
                surface_list.push_back(surf_data);
        }
        surfaces.clear();

        for (auto& [_, surf_data] : egl_surfaces) {
            if (surf_data)
                surface_list.push_back(surf_data);
        }
        egl_surfaces.clear();
    }

    active_surface.reset();
    for (auto& surf_data : surface_list)
        surf_data->destroy_wayland_objects();

    {
        std::lock_guard lock(seats_m);
        seats.clear();
    }
}

void Wayland::remove_seat(uint32_t name, wl_display* display)
{
    {
        std::lock_guard lock(seats_m);
        for (auto it = seats.begin(); it != seats.end();) {
            auto& seat = *it;
            if (seat->registry_name == name && seat->display == display) {
                seat->destroy();
                it = seats.erase(it);
            } else {
                ++it;
            }
        }
    }

    update_focus();
}

void Wayland::update_import(const std::shared_ptr<surface_data>& surf_data)
{
    if (!surf_data || !surf_data->display)
        return;

    std::lock_guard lock(surf_data->m);
    auto* globals = ctx.get_global(surf_data->display);
    if (!globals || !globals->dmabuf)
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
        wl_display_roundtrip_queue(surf_data->display, globals->queue);
        surf_data->attached = false;
    }
    surf_data->buffers.clear();
    ipc->clear_frames();

    for (auto [idx, dmabuf] : enumerate(fdinfo.dmabuf_buffer)) {
        std::shared_ptr<shm_buffer> buf = std::make_shared<shm_buffer>();
        buf->ipc = ipc.get();
        buf->idx = idx;
        buf->dmabuf_fd = unique_fd::adopt(dup(dmabuf.get()));
        buf->width = fdinfo.w;
        buf->height = fdinfo.h;
        buf->stride = fdinfo.stride;
        buf->offset = fdinfo.dmabuf_offset;
        buf->modifier = fdinfo.modifier;
        auto* params = zwp_linux_dmabuf_v1_create_params(globals->dmabuf);
        zwp_linux_buffer_params_v1_add(params, dmabuf.get(), 0, fdinfo.dmabuf_offset,
                                       fdinfo.stride, fdinfo.modifier >> 32,
                                       fdinfo.modifier & 0xffffffff);

        buf->buffer = zwp_linux_buffer_params_v1_create_immed(params, fdinfo.w,
                                                              fdinfo.h,
                                                              fdinfo.fourcc, 0);

        wl_buffer_add_listener(buf->buffer, &Wayland::buffer_listener, buf.get());

        zwp_linux_buffer_params_v1_destroy(params);
        surf_data->buffers.push_back(std::move(buf));
    }

    imported_generation = generation;

    for (auto& buf : surf_data->buffers)
        release_to_server(buf.get());
}

void Wayland::dispatch_events(const std::shared_ptr<surface_data>& surf_data)
{
    update_focus();

    if (!surf_data || !surf_data->display)
        return;

    auto* globals = ctx.get_global(surf_data->display);
    if (!globals || !globals->queue)
        return;

    wl_display_dispatch_queue_pending(surf_data->display, globals->queue);
    wl_display_flush(surf_data->display);
}

void Wayland::update_focus()
{
    bool is_focused = false;
    bool keyboard_active = false;
    bool pointer_active = false;
    bool presentation_active = false;
    std::vector<std::string> focused_seats;
    uint64_t now = os_time_get_nano();
    {
        std::lock_guard lock(seats_m);
        for (auto& seat : seats) {
            auto snapshot = seat->update(now);
            keyboard_active |= snapshot.focus.keyboard.active;
            pointer_active |= snapshot.focus.pointer.active;
            presentation_active |= snapshot.focus.presentation.active;
            if (snapshot.focus.focused())
                focused_seats.push_back(snapshot.identifier);
        }
    }

    is_focused = !focused_seats.empty();
    if (ipc) {
        if (ipc->set_focused_seats(focused_seats)) {
            SPDLOG_DEBUG("wl focus changed: focused={} seats={} keyboard={} pointer={} presentation={}",
                         is_focused, fmt::join(focused_seats, ","),
                         keyboard_active, pointer_active, presentation_active);
        }
    }
}

void Wayland::set_presentation_focus(bool active, uint64_t time_ns)
{
    {
        std::lock_guard lock(seats_m);
        for (auto& seat : seats)
            seat->set_presentation(active, time_ns);
    }

    update_focus();
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
    auto* globals = ctx.get_global(surf_data->display);
    if (globals)
        request_presentation_feedback(surf_data, *globals, surf_data->overlay_surf, SampleType::Hud, hud_surface_name);
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
        auto* globals = ctx.get_global(surf_data->display);
        if (globals && globals->queue)
            wl_display_roundtrip_queue(surf_data->display, globals->queue);
    }
    surf_data->attached = false;
}

void Wayland::run_thread(std::shared_ptr<surface_data> surf_data)
{
    while (!quit.load()) {
        dispatch_events(surf_data);
        if (!ipc->connected.load(std::memory_order_acquire)) {
            detach(surf_data);
        } else {
            update_import(surf_data);
            present(surf_data);
            dispatch_events(surf_data);
        }

        if (quit.load())
            break;

        int sleep = ipc->connected.load(std::memory_order_acquire) ? 4 : 100;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep));
    }
}
