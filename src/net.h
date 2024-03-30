#pragma once
#include <vector>
#include <string>
#include <stdint.h>
#include "filesystem.h"
#include "file_utils.h"
#include <spdlog/spdlog.h>
#include <iostream>

namespace fs = ghc::filesystem;

#ifndef NETDIR
#define NETDIR "/sys/class/net/"
#endif

#ifndef TXFILE
#define TXFILE "/statistics/tx_bytes"
#endif

#ifndef RXFILE
#define RXFILE "/statistics/rx_bytes"
#endif

class Net {
    public:
        bool should_reset = false;
        struct interface {
            std::string name;
            uint64_t txBytes;
            uint64_t rxBytes;
            uint64_t txBps;
            uint64_t rxBps;
            std::chrono::steady_clock::time_point previousTime;
        };

        Net();
        void update();
        std::vector<interface> interfaces = {};

    private:
        uint64_t calculateThroughput(long long currentBytes, long long previousBytes,
                            std::chrono::steady_clock::time_point previousTime,
                            std::chrono::steady_clock::time_point currentTime);
};

extern std::unique_ptr<Net> net;