#include "net.h"
#include "hud_elements.h"

Net::Net() {
    auto params = get_params();
    should_reset = false;
    fs::path net_dir(NETDIR);
    if (fs::exists(net_dir) && fs::is_directory(net_dir)) {
        for (const auto& entry : fs::directory_iterator(net_dir)) {
            if (fs::is_directory(entry.status())) {
                auto val = entry.path().filename().string();
                if (val == "lo")
                    continue;

                if (!params->network.empty() && params->network.front() == "1") {
                    interfaces.push_back({entry.path().filename().string(), 0, 0});
                } else if (!params->network.empty()){
                    auto it = std::find(params->network.begin(), params->network.end(), val);
                    if (it != params->network.end())
                        interfaces.push_back({entry.path().filename().string(), 0, 0});
                }
            }
        }
    }
    
    if (interfaces.empty())
        SPDLOG_ERROR("Network: couldn't find any interfaces");
}

long long safe_stoll(const std::string& str, long long default_value);
long long safe_stoll(const std::string& str, long long default_value = 0) {
    if (str.empty()) {
        SPDLOG_DEBUG("tx or rx returned an empty string");
        return default_value;
    }

    try {
        return std::stoll(str);
    } catch (const std::invalid_argument& e) {
        SPDLOG_DEBUG("stoll invalid argument");
    } catch (const std::out_of_range& e) {
        SPDLOG_DEBUG("stoll out of range");
    }
    return default_value;
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
            iface.txBytes = safe_stoll(read_line(txfile));
            iface.rxBytes = safe_stoll(read_line(rxfile));

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
