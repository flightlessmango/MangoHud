#pragma once

#include <wayland-client.h>

#include <atomic>
#include <cstdint>

#include "../server/common/helpers.hpp"
#include "linux-dmabuf-v1-client-protocol.h"

struct wl_globals;

struct wl_dmabuf_info {
    unique_fd fd;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;
    uint32_t fourcc = 0;
    uint64_t modifier = 0;
};

struct wl_buffer_slot {
    wl_buffer* buffer = nullptr;
    unique_fd dmabuf_fd;
    int idx = -1;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;
    uint32_t fourcc = 0;
    uint64_t modifier = 0;
    std::atomic<bool> busy{false};

    wl_buffer_slot() = default;
    wl_buffer_slot(const wl_buffer_slot&) = delete;
    wl_buffer_slot& operator=(const wl_buffer_slot&) = delete;

    ~wl_buffer_slot();
};

bool wl_import_dmabuf(zwp_linux_dmabuf_v1* dmabuf, wl_buffer_slot& slot, wl_dmabuf_info info);
wl_surface* wl_create_surface(wl_globals& globals);
unique_fd wl_export_dmabuf_sync_fd(int dmabuf_fd);
