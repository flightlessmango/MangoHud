#pragma once
#include <EGL/egl.h>
#include <gbm.h>
#include "shared.h"
#include <spdlog/spdlog.h>

inline bool create_gbm(clientRes* r, dmabuf_t* buf, int dev_fd, const uint64_t modifier) {
    buf->gbm = {};
    buf->gbm.fourcc = DRM_FORMAT_ARGB8888;
    buf->gbm.dev = gbm_create_device(dev_fd);
    if (!buf->gbm.dev) {
        SPDLOG_ERROR("gbm_create_device failed for fd={}", dev_fd);
        return false;
    }

    SPDLOG_INFO("gbm alloc: fd={}, w={}, h={}, fourcc=0x{:x}", dev_fd, r->w, r->h, buf->gbm.fourcc);

    buf->gbm.bo = gbm_bo_create_with_modifiers(buf->gbm.dev, r->w, r->h, buf->gbm.fourcc, &modifier, 1);
    if (!buf->gbm.bo) {
        SPDLOG_ERROR("gbm_bo_create_with_modifiers failed for modifier=0x{:016x}", modifier);
        return false;
    }

    buf->gbm.fd = unique_fd::adopt(gbm_bo_get_fd(buf->gbm.bo));
    if (!buf->gbm.fd) {
        SPDLOG_ERROR("gbm_bo_get_fd failed");
        buf->gbm = {};
        return false;
    }

    buf->gbm.modifier = gbm_bo_get_modifier(buf->gbm.bo);
    if (buf->gbm.modifier != modifier) {
        SPDLOG_ERROR("Expected {} modifier, got 0x{:016x}", modifier, buf->gbm.modifier);
        buf->gbm = {};
        return false;
    }

    buf->gbm.stride   = gbm_bo_get_stride_for_plane(buf->gbm.bo, 0);
    buf->gbm.offset   = gbm_bo_get_offset(buf->gbm.bo, 0);
    buf->gbm.plane_size = (uint64_t)buf->gbm.stride * (uint64_t)r->h;

    SPDLOG_INFO("gbm bo: fourcc=0x{:x}, modifier=0x{:x}, planes={}",
        gbm_bo_get_format(buf->gbm.bo),
        gbm_bo_get_modifier(buf->gbm.bo),
        gbm_bo_get_plane_count(buf->gbm.bo));

    SPDLOG_INFO("gbm plane 0: fd={}, offset={}, stride={}",
        buf->gbm.fd.get(),
        gbm_bo_get_offset(buf->gbm.bo, 0),
        gbm_bo_get_stride_for_plane(buf->gbm.bo, 0));
    return true;
}
