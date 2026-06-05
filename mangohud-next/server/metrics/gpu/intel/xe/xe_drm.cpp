#include <stdio.h>
#include <stdint.h>
#include "include/xe_drm.h" // change this when libdrm-dev adds xe_drm.h
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/capability.h>
#include <unistd.h>

#include <spdlog/spdlog.h>
#include "xe_drm.hpp"

static int intel_ioctl(int fd, unsigned long request, void *arg) {
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

    return ret;
}

static void *xe_device_query_alloc_fetch(int fd, uint32_t query_id, uint32_t *len) {
    struct drm_xe_device_query query = {
        .query = query_id,
    };

    if (intel_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query))
        return NULL;

    void* data = calloc(1, query.size);

    if (!data)
        return NULL;

    query.data = (uintptr_t)data;

    if (intel_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query)) {
        free(data);
        return NULL;
    }

    if (len)
        *len = query.size;

    return data;
}

static bool is_capability_available(int capability) {
    cap_t cap = cap_get_proc();
    cap_flag_value_t cap_enabled = {};

    cap_get_flag(cap, capability, CAP_EFFECTIVE, &cap_enabled);
    cap_free(cap);

    return static_cast<bool>(cap_enabled);
}

xe_drm_base::xe_drm_base() {
    has_cap_perfmon = is_capability_available(CAP_PERFMON);
    SPDLOG_DEBUG("has_cap_perfmon = {}", has_cap_perfmon);
}

bool xe_drm_base::setup(const std::string& card) {
    card_fd = open(card.c_str(), O_WRONLY);

    if (card_fd < 0) {
        SPDLOG_ERROR("Failed to open {}.", card);
        return false;
    }

    return true;
}

void xe_drm_base::poll() {
    uint32_t length = 0;
    void* ptr = xe_device_query_alloc_fetch(card_fd, DRM_XE_DEVICE_QUERY_MEM_REGIONS, &length);
    drm_xe_query_mem_regions *regions = reinterpret_cast<drm_xe_query_mem_regions*>(ptr);

    if (!regions) {
        SPDLOG_TRACE("regions = {}", static_cast<void*>(regions));
        return;
    }

    for (uint8_t i = 0; i < regions->num_mem_regions; i++) {
        drm_xe_mem_region mr = regions->mem_regions[i];

        if (mr.mem_class != DRM_XE_MEM_REGION_CLASS_VRAM)
            continue;

        total_memory = mr.total_size;
        used_memory  = mr.used;

        SPDLOG_TRACE("total_memory = {} MiB", total_memory / 1024.f / 1024.f);
        SPDLOG_TRACE("used_memory  = {} MiB", used_memory / 1024.f / 1024.f);

        break;
    }

    return;
}

uint64_t xe_drm_base::get_total_memory() const {
    return total_memory;
}

uint64_t xe_drm_base::get_used_memory() const {
    if (!has_cap_perfmon && geteuid() != 0)
        return 0;

    return used_memory;
}

xe_drm::xe_drm() {}
