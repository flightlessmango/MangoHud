#include "net.h"
#include "hud_elements.h"

Net::Net() {
    should_reset = false;
    fs::path net_dir(NETDIR);
    if (fs::exists(net_dir) && fs::is_directory(net_dir)) {
        for (const auto& entry : fs::directory_iterator(net_dir)) {
            if (fs::is_directory(entry.status())) {
                auto val = entry.path().filename().string();
                if (val == "lo")
                    continue;

                if (!HUDElements.params->network.empty() && HUDElements.params->network.front() == "1") {
                    interfaces.push_back({entry.path().filename().string(), 0, 0});
                } else if (!HUDElements.params->network.empty()){
                    auto it = std::find(HUDElements.params->network.begin(), HUDElements.params->network.end(), val);
                    if (it != HUDElements.params->network.end())
                        interfaces.push_back({entry.path().filename().string(), 0, 0});
                }
            }
        }
    }
    
    if (interfaces.empty())
        SPDLOG_ERROR("Network: couldn't find any interfaces");
}

void Net::update() {
    if (!interfaces.empty()) {
        for (auto& iface : interfaces) {
            // path to tx_bytes and rx_bytes
            std::string txfile = (NETDIR + iface.name + TXFILE);
            std::string rxfile = (NETDIR + iface.name + RXFILE);
            
            // amount of bytes at previous update
            uint64_t prevTx = iface.txBytes;
            uint64_t prevRx = iface.rxBytes;
            
            // current amount of bytes
            iface.txBytes = std::stoll(read_line(txfile));
            iface.rxBytes = std::stoll(read_line(rxfile));

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