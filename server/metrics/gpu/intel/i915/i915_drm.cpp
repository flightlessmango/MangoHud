#include <stdio.h>
#include <stdint.h>
#include <libdrm/i915_drm.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/capability.h>
#include <unistd.h>

#include <spdlog/spdlog.h>
#include "i915_drm.hpp"

static int intel_ioctl(int fd, unsigned long request, void *arg) {
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

    return ret;
}

static int intel_i915_query(int fd, uint64_t query_id, void *buffer, int32_t *buffer_len) {
    struct drm_i915_query_item item = {
        .query_id = query_id,
        .length = *buffer_len,
        .flags = 0,
        .data_ptr = reinterpret_cast<uintptr_t>(buffer),
    };

    struct drm_i915_query args = {
        .num_items = 1,
        .flags = 0,
        .items_ptr = reinterpret_cast<uintptr_t>(&item),
    };

    int ret = intel_ioctl(fd, DRM_IOCTL_I915_QUERY, &args);

    if (ret != 0)
        return -errno;
    else if (item.length < 0)
        return item.length;

    *buffer_len = item.length;

    return 0;
}

static std::vector<void*> intel_i915_query_alloc(int fd, uint64_t query_id, int32_t *query_length) {
    if (query_length)
        *query_length = 0;

    int32_t length = 0;
    int ret = intel_i915_query(fd, query_id, NULL, &length);

    if (ret < 0)
        return {};

    std::vector<void*> data(length / sizeof(void*) + 1);

    ret = intel_i915_query(fd, query_id, data.data(), &length);

    if (ret < 0)
        return {};

    if (query_length)
        *query_length = length;

    return data;
}

static bool is_capability_available(int capability) {
    cap_t cap = cap_get_proc();
    cap_flag_value_t cap_enabled = {};

    cap_get_flag(cap, capability, CAP_EFFECTIVE, &cap_enabled);
    cap_free(cap);

    return static_cast<bool>(cap_enabled);
}

i915_drm_base::i915_drm_base() {
    has_cap_perfmon = is_capability_available(CAP_PERFMON);
    SPDLOG_DEBUG("has_cap_perfmon = {}", has_cap_perfmon);
}

bool i915_drm_base::setup(const std::string& card) {
    card_fd = open(card.c_str(), O_WRONLY);

    if (card_fd < 0) {
        SPDLOG_ERROR("Failed to open {}.", card);
        return false;
    }

    return true;
}

void i915_drm_base::poll() {
    int32_t length = 0;

    std::vector<void*> ptr =
        intel_i915_query_alloc(card_fd, DRM_I915_QUERY_MEMORY_REGIONS, &length);

    drm_i915_query_memory_regions* regions =
        reinterpret_cast<drm_i915_query_memory_regions*>(ptr.data());

    if (ptr.empty()) {
        SPDLOG_TRACE("regions = {}", static_cast<void*>(regions));
        return;
    }

    for (uint8_t i = 0; i < regions->num_regions; i++) {
        drm_i915_memory_region_info mr = regions->regions[i];

        if (mr.region.memory_class != I915_MEMORY_CLASS_DEVICE)
            continue;

        total_memory = mr.probed_size;
        free_memory  = mr.unallocated_size;
        used_memory  = total_memory - free_memory;

        SPDLOG_TRACE("total_memory = {} MiB", total_memory / 1024.f / 1024.f);
        SPDLOG_TRACE("free_memory  = {} MiB", free_memory / 1024.f / 1024.f);
        SPDLOG_TRACE("used_memory  = {} MiB", used_memory / 1024.f / 1024.f);

        break;
    }

    return;
}

uint64_t i915_drm_base::get_total_memory() const {
    return total_memory;
}

uint64_t i915_drm_base::get_used_memory() const {
    if (!has_cap_perfmon && geteuid() != 0)
        return 0;

    return used_memory;
}

i915_drm::i915_drm() {}
