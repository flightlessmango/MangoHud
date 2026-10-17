#pragma once

#include <string>
#include <stdint.h>

class xe_drm_base {
private:
    int card_fd = 0;
    uint64_t total_memory = 0;
    uint64_t used_memory = 0;
    // Integrated GPUs have no VRAM region; their buffers live in the sysmem region.
    bool has_vram = false;
    uint64_t sysmem_total = 0;
    uint64_t sysmem_used = 0;

public:
    xe_drm_base();
    bool setup(const std::string& card);
    void poll();
    uint64_t get_total_memory() const;
    uint64_t get_used_memory() const;
    bool has_vram_region() const;
    uint64_t get_sysmem_total() const;
    uint64_t get_sysmem_used() const;
};

struct xe_drm {
    xe_drm_base drm;
    xe_drm();
};
