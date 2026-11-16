#pragma once
#include <vector>
#include <string>
#include <stdint.h>
#include "filesystem.h"
#include "file_utils.h"
#include <spdlog/spdlog.h>
#include <iostream>

namespace fs = ghc::filesystem;

// Platform backend (net_linux.cpp, net_getifaddrs.cpp)
// Names of the non-loopback network interfaces.
std::vector<std::string> net_list_interfaces();
// Cumulative bytes transmitted/received by an interface.
void net_read_counters(const std::string& name, uint64_t& tx, uint64_t& rx);

class Net {
    public:
        bool should_reset = false;
        struct networkInterface {
            std::string name;
            uint64_t txBytes;
            uint64_t rxBytes;
            uint64_t txBps;
            uint64_t rxBps;
            std::chrono::steady_clock::time_point previousTime;
        };

        Net();
        void update();
        std::vector<networkInterface> interfaces = {};

    private:
        uint64_t calculateThroughput(long long currentBytes, long long previousBytes,
                            std::chrono::steady_clock::time_point previousTime,
                            std::chrono::steady_clock::time_point currentTime);
};

extern std::unique_ptr<Net> net;