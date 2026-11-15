#pragma once
#ifndef MANGOHUD_RAPL_H
#define MANGOHUD_RAPL_H

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <spdlog/spdlog.h>
#include "file_utils.h"
#include "timing.hpp"

// Reads a powercap (Intel/AMD RAPL) energy_uj counter and turns it into watts.
// Handles counter wraparound via max_energy_range_uj.
struct RaplCounter {
    FILE* file = nullptr;
    uint64_t max_range = 0;
    uint64_t last_value = 0;
    Clock::time_point last_time;
    bool primed = false;

    RaplCounter() = default;
    RaplCounter(const RaplCounter&) = delete;
    RaplCounter& operator=(const RaplCounter&) = delete;
    ~RaplCounter() { close(); }

    void close() {
        if (file)
            fclose(file);
        file = nullptr;
        primed = false;
    }

    bool open(const std::string& domain_path) {
        close();
        std::string energy = domain_path + "/energy_uj";
        file = fopen(energy.c_str(), "r");
        if (!file) {
            if (errno == EACCES)
                SPDLOG_WARN("RAPL: {} is not readable (root-only by default). "
                            "Allow reading it with a udev rule to get power readings.", energy);
            else
                SPDLOG_DEBUG("RAPL: cannot open {}: {}", energy, strerror(errno));
            return false;
        }

        std::string range = read_line(domain_path + "/max_energy_range_uj");
        max_range = range.empty() ? 0 : strtoull(range.c_str(), nullptr, 10);
        return true;
    }

    bool is_open() const { return file != nullptr; }

    // Returns false when no reading is available. The first successful call only
    // primes the counter and reports 0 W.
    bool read(float& watts) {
        if (!file)
            return false;

        rewind(file);
        fflush(file);

        uint64_t value = 0;
        if (fscanf(file, "%" SCNu64, &value) != 1)
            return false;

        Clock::time_point now = Clock::now();
        watts = 0.f;

        if (primed) {
            int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(now - last_time).count();
            uint64_t delta;
            if (value >= last_value)
                delta = value - last_value;
            else if (max_range > last_value)
                delta = max_range - last_value + value;
            else
                delta = 0;

            if (us > 0)
                watts = (float)((double)delta / (double)us);
        }

        last_value = value;
        last_time = now;
        primed = true;
        return true;
    }
};

// Finds the powercap domain with the given name ("package-0", "uncore", "core"...),
// preferring the MSR based intel-rapl/amd-rapl zones over intel-rapl-mmio.
static inline std::string find_rapl_domain(const std::string& domain_name) {
    const std::string powercap = "/sys/class/powercap/";
    std::string fallback;

    for (auto& dir : ls(powercap.c_str())) {
        std::string path = powercap + dir;
        if (read_line(path + "/name") != domain_name)
            continue;

        if (dir.find("mmio") == std::string::npos)
            return path;

        if (fallback.empty())
            fallback = path;
    }

    return fallback;
}

#endif // MANGOHUD_RAPL_H
