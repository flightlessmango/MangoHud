#include "net.h"

#ifndef NETDIR
#define NETDIR "/sys/class/net/"
#endif

#ifndef TXFILE
#define TXFILE "/statistics/tx_bytes"
#endif

#ifndef RXFILE
#define RXFILE "/statistics/rx_bytes"
#endif

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

std::vector<std::string> net_list_interfaces() {
    std::vector<std::string> names;
    std::error_code ec;
    fs::path net_dir(NETDIR);
    if (!fs::is_directory(net_dir, ec))
        return names;

    std::error_code iterator_ec;
    for (fs::directory_iterator it(net_dir, iterator_ec), end;
         !iterator_ec && it != end; it.increment(iterator_ec)) {
        const auto& entry = *it;
        if (!entry.is_directory(ec))
            continue;
        auto val = entry.path().filename().string();
        if (val == "lo")
            continue;
        names.push_back(val);
    }
    if (iterator_ec)
        SPDLOG_DEBUG("Failed to iterate {}: {}", net_dir.string(), iterator_ec.message());
    return names;
}

void net_read_counters(const std::string& name, uint64_t& tx, uint64_t& rx) {
    tx = safe_stoll(read_line(NETDIR + name + TXFILE));
    rx = safe_stoll(read_line(NETDIR + name + RXFILE));
}
