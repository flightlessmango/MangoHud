#include "net.h"
#include <algorithm>
#include "hud_elements.h"

Net::Net() {
    auto params = get_params();
    should_reset = false;
    for (const auto& val : net_list_interfaces()) {
        if (params->network.empty())
            continue;
        if (params->network.front() == "1" ||
            std::find(params->network.begin(), params->network.end(), val) != params->network.end())
            interfaces.push_back({val, 0, 0});
    }

    if (interfaces.empty())
        SPDLOG_ERROR("Network: couldn't find any interfaces");
}

void Net::update() {
    if (!interfaces.empty()) {
        for (auto& iface : interfaces) {
            // amount of bytes at previous update
            uint64_t prevTx = iface.txBytes;
            uint64_t prevRx = iface.rxBytes;
            
            // current amount of bytes
            net_read_counters(iface.name, iface.txBytes, iface.rxBytes);

            auto now = std::chrono::steady_clock::now();
            // calculate the bytes per second since last update
            iface.txBps = calculateThroughput(iface.txBytes, prevTx, iface.previousTime, now);
            iface.rxBps = calculateThroughput(iface.rxBytes, prevRx, iface.previousTime, now);
            iface.previousTime = now;
        }
    }
}

uint64_t Net::calculateThroughput(long long currentBytes, long long previousBytes,
                                std::chrono::steady_clock::time_point previousTime,
                                std::chrono::steady_clock::time_point currentTime) {
    std::chrono::duration<double> elapsed = (currentTime - previousTime);
    return static_cast<long long>((currentBytes - previousBytes) / elapsed.count());
}
