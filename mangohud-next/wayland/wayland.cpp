#include "wayland.h"
#include "wayland_ctx.h"

#include <cerrno>
#include <linux/dma-buf.h>
#include <spdlog/spdlog.h>
#include <utility>
#include <sys/ioctl.h>

wl_buffer_slot::~wl_buffer_slot()
{
    if (buffer) {
        wl_buffer_destroy(buffer);
        buffer = nullptr;
    }
}

bool wl_import_dmabuf(zwp_linux_dmabuf_v1* dmabuf, wl_buffer_slot& slot, wl_dmabuf_info info)
{
    if (!dmabuf || !info.fd || info.width == 0 || info.height == 0)
        return false;

    auto* params = zwp_linux_dmabuf_v1_create_params(dmabuf);
    if (!params)
        return false;

    zwp_linux_buffer_params_v1_add(params, info.fd.get(), 0, info.offset,
                                   info.stride, info.modifier >> 32,
                                   info.modifier & 0xffffffff);

    wl_buffer* buffer = zwp_linux_buffer_params_v1_create_immed(params, info.width,
                                                               info.height,
                                                               info.fourcc, 0);
    zwp_linux_buffer_params_v1_destroy(params);

    if (!buffer)
        return false;

    if (slot.buffer)
        wl_buffer_destroy(slot.buffer);

    slot.buffer = buffer;
    slot.dmabuf_fd = std::move(info.fd);
    slot.width = info.width;
    slot.height = info.height;
    slot.stride = info.stride;
    slot.offset = info.offset;
    slot.fourcc = info.fourcc;
    slot.modifier = info.modifier;
    slot.busy.store(false, std::memory_order_release);
    return true;
}

wl_surface* wl_create_surface(wl_globals& globals)
{
    if (!globals.compositor)
        return nullptr;

    auto* surface = wl_compositor_create_surface(globals.compositor);
    if (!surface)
        return nullptr;

    if (globals.queue)
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(surface), globals.queue);

    return surface;
}

unique_fd wl_export_dmabuf_sync_fd(int dmabuf_fd)
{
    if (dmabuf_fd < 0)
        return {};

    dma_buf_export_sync_file sync_file{};
    sync_file.flags = DMA_BUF_SYNC_READ;
    sync_file.fd = -1;

    if (ioctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &sync_file) != 0) {
        SPDLOG_ERROR("DMA_BUF_IOCTL_EXPORT_SYNC_FILE failed: errno={}", errno);
        return {};
    }

    if (sync_file.fd < 0) {
        SPDLOG_ERROR("DMA_BUF_IOCTL_EXPORT_SYNC_FILE returned invalid fd");
        return {};
    }

    return unique_fd::adopt(sync_file.fd);
}
