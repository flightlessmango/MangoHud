#pragma once

#include <string>
#include <stdint.h>

class xe_drm_base {
private:
    bool has_cap_perfmon = false;
    int card_fd = 0;
    uint64_t total_memory = 0;
    uint64_t used_memory = 0;

public:
    xe_drm_base();
    bool setup(const std::string& card);
    void poll();
    uint64_t get_total_memory() const;
    uint64_t get_used_memory() const;
};

struct xe_drm {
    xe_drm_base drm;
    xe_drm();
};
