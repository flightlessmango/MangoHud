#include "net.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <ifaddrs.h>
#include <algorithm>

// Interface counters come from the link-level (AF_LINK) entry of getifaddrs(),
// which is available on every BSD.

std::vector<std::string> net_list_interfaces() {
    std::vector<std::string> names;
    struct ifaddrs *ifap = nullptr;
    if (getifaddrs(&ifap) != 0) {
        SPDLOG_DEBUG("getifaddrs failed");
        return names;
    }

    for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK)
            continue;
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;
        std::string name = ifa->ifa_name;
        if (std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(name);
    }
    freeifaddrs(ifap);
    return names;
}

void net_read_counters(const std::string& name, uint64_t& tx, uint64_t& rx) {
    tx = rx = 0;
    struct ifaddrs *ifap = nullptr;
    if (getifaddrs(&ifap) != 0)
        return;

    for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK || !ifa->ifa_data)
            continue;
        if (name != ifa->ifa_name)
            continue;
        const struct if_data *d = static_cast<const struct if_data *>(ifa->ifa_data);
        tx = d->ifi_obytes;
        rx = d->ifi_ibytes;
        break;
    }
    freeifaddrs(ifap);
}
