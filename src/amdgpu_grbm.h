#pragma once
#ifndef MANGOHUD_AMDGPU_GRBM_H
#define MANGOHUD_AMDGPU_GRBM_H

// Kernel-only GPU busy% for amdgpu GPUs that do not support the sysfs
// `gpu_busy_percent` file (e.g. Southern Islands such as Oland / Radeon R7 240,
// where reading it fails with EOPNOTSUPP).
//
// The kernel still lets an unprivileged process read the GRBM_STATUS register
// through the DRM render node (AMDGPU_INFO_READ_MMR_REG) - this is what
// radeontop does through libdrm. Here the ioctl is issued directly, so MangoHud
// gets no new dependency.
//
// GRBM_STATUS says which blocks are busy at one instant; bit 31 (GUI_ACTIVE) is
// "the graphics pipe is busy" and has the same position on every GCN/RDNA GPU.
// Utilisation is the fraction of reads in which the bit was set, so a small
// background thread reads the register a few hundred times per second and keeps
// two cumulative counters. percent() diffs them against the previous call.
//
// Ported from gpu-probe (src/providers/grbm.rs).

#include <atomic>
#include <cstdint>
#include <chrono>
#include <string>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

class AMDGPUGrbm {
public:
    // GRBM_STATUS byte offset 0x8010, as a dword index.
    static constexpr uint32_t GRBM_STATUS_DWORD = 0x8010 / 4;
    static constexpr uint32_t GUI_ACTIVE_MASK = 1u << 31;
    // ~250 reads per second.
    static constexpr int SAMPLE_PERIOD_MS = 4;
    // Reads needed in a window before a percentage is trusted.
    static constexpr uint64_t MIN_SAMPLES = 25;
    static constexpr uint32_t MAX_CONSECUTIVE_ERRORS = 100;

    // struct drm_amdgpu_info from include/uapi/drm/amdgpu_drm.h, with the
    // read_mmr_reg member of its union spelled out (32 bytes either way).
    struct DrmAmdgpuInfo {
        uint64_t return_pointer;
        uint32_t return_size;
        uint32_t query;
        uint32_t dword_offset;
        uint32_t count;
        uint32_t instance;
        uint32_t flags;
    };

    static constexpr unsigned long DRM_COMMAND_BASE = 0x40;
    static constexpr unsigned long DRM_AMDGPU_INFO = 0x05;
    static constexpr uint32_t AMDGPU_INFO_READ_MMR_REG = 0x15;
    // _IOW('d', DRM_COMMAND_BASE + DRM_AMDGPU_INFO, struct drm_amdgpu_info)
    static constexpr unsigned long DRM_IOCTL_AMDGPU_INFO =
        _IOW('d', DRM_COMMAND_BASE + DRM_AMDGPU_INFO, DrmAmdgpuInfo);

    // Pure helper (unit-testable): percent of reads with the bit set between two
    // counter snapshots. Returns false if there are too few reads in between.
    static bool percent_between(uint64_t total_prev, uint64_t active_prev,
                                uint64_t total_now, uint64_t active_now,
                                uint64_t min_samples, int &out_percent) {
        if (total_now < total_prev)
            return false;
        uint64_t total = total_now - total_prev;
        if (total < (min_samples ? min_samples : 1))
            return false;
        uint64_t active = active_now >= active_prev ? active_now - active_prev : 0;
        if (active > total)
            active = total; // counters raced; never exceed 100%
        out_percent = int((active * 100 + total / 2) / total);
        return true;
    }

    static bool read_register(int fd, uint32_t dword_offset, uint32_t &value) {
        value = 0;
        DrmAmdgpuInfo req{};
        req.return_pointer = reinterpret_cast<uintptr_t>(&value);
        req.return_size = sizeof(value);
        req.query = AMDGPU_INFO_READ_MMR_REG;
        req.dword_offset = dword_offset;
        req.count = 1;
        req.instance = 0xffffffffu; // all shader engines, as libdrm callers pass
        req.flags = 0;
        int rc;
        do {
            rc = ioctl(fd, DRM_IOCTL_AMDGPU_INFO, &req);
        } while (rc != 0 && (errno == EINTR || errno == EAGAIN));
        return rc == 0;
    }

    // `node` is e.g. "/dev/dri/renderD128". Fails (valid() == false) when the node
    // can't be opened or the kernel refuses the read, so MangoHud never shows
    // made-up numbers.
    explicit AMDGPUGrbm(const std::string &node) {
        fd = open(node.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0)
            fd = open(node.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            last_errno = errno;
            return;
        }
        uint32_t probe;
        if (!read_register(fd, GRBM_STATUS_DWORD, probe)) {
            last_errno = errno;
            close(fd);
            fd = -1;
            return;
        }
        thread = std::thread(&AMDGPUGrbm::run, this);
    }

    ~AMDGPUGrbm() {
        stop = true;
        if (thread.joinable())
            thread.join();
        if (fd >= 0)
            close(fd);
    }

    AMDGPUGrbm(const AMDGPUGrbm &) = delete;
    AMDGPUGrbm &operator=(const AMDGPUGrbm &) = delete;

    // True while the sampler thread is running and reads succeed.
    bool valid() const { return fd >= 0 && alive; }
    int error() const { return last_errno; }

    void pause() { paused = true; }
    void resume() { paused = false; }

    // Busy percent since the previous successful call. Returns false (and leaves
    // `out_percent` alone) when there are not enough samples yet.
    bool percent(int &out_percent) {
        if (!valid())
            return false;
        // `total` is read first: the thread bumps it before `active`.
        uint64_t total = total_reads.load(std::memory_order_relaxed);
        uint64_t active = active_reads.load(std::memory_order_relaxed);
        if (!percent_between(prev_total, prev_active, total, active, MIN_SAMPLES, out_percent))
            return false;
        prev_total = total;
        prev_active = active;
        return true;
    }

private:
    void run() {
        uint32_t errors = 0;
        while (!stop) {
            if (paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            uint32_t status;
            if (read_register(fd, GRBM_STATUS_DWORD, status)) {
                errors = 0;
                total_reads.fetch_add(1, std::memory_order_relaxed);
                if (status & GUI_ACTIVE_MASK)
                    active_reads.fetch_add(1, std::memory_order_relaxed);
            } else if (++errors >= MAX_CONSECUTIVE_ERRORS) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(SAMPLE_PERIOD_MS));
        }
        alive = false;
    }

    int fd = -1;
    int last_errno = 0;
    std::atomic<bool> stop{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> alive{true};
    std::atomic<uint64_t> total_reads{0};
    std::atomic<uint64_t> active_reads{0};
    uint64_t prev_total = 0;
    uint64_t prev_active = 0;
    std::thread thread;
};

#endif // MANGOHUD_AMDGPU_GRBM_H
