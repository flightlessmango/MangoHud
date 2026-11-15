#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <kvm.h>
#include <unistd.h>
#include <algorithm>
#include <cstdint>
#include <spdlog/spdlog.h>

#include "memory.h"
#include "hud_elements.h"
#include "sysctl_utils.h"

static constexpr float GiB = 1024.f * 1024.f * 1024.f;

// "Available" is free plus inactive pages, the pages the VM hands out without
// paging anything. Wired memory, ZFS ARC included, counts as used.
void update_meminfo() {
    uint32_t page_size = 0, total = 0, free_pages = 0, inactive = 0;

    if (!sysctl_read("vm.stats.vm.v_page_size", page_size) ||
        !sysctl_read("vm.stats.vm.v_page_count", total) ||
        !sysctl_read("vm.stats.vm.v_free_count", free_pages) ||
        !sysctl_read("vm.stats.vm.v_inactive_count", inactive)) {
        SPDLOG_ERROR("can't read vm.stats.vm");
        return;
    }

    const uint32_t available = std::min(total, free_pages + inactive);
    memmax = static_cast<float>(total) * page_size / GiB;
    memused = static_cast<float>(total - available) * page_size / GiB;

    // NULL kvm descriptor: uses sysctl vm.swap_info, no privileges needed.
    // With maxswap 1 the only entry holds the totals of all devices, in pages.
    struct kvm_swap swap {};
    if (kvm_getswapinfo(nullptr, &swap, 1, 0) >= 0)
        swapused = static_cast<float>(swap.ksw_used) * page_size / GiB;
}

// No sensor driver for RAM temperatures is supported, mem_temp stays at -1
void update_mem_temp() {
}

// Shared memory is not reported: it would need a walk of the whole VM map
void update_procmem()
{
    auto gs_pid = HUDElements.g_gamescopePid;
    const int pid = gs_pid < 1 ? static_cast<int>(getpid()) : static_cast<int>(gs_pid);

    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };
    struct kinfo_proc kp;
    size_t len = sizeof(kp);

    if (sysctl(mib, 4, &kp, &len, nullptr, 0) != 0 || len < sizeof(kp)) {
        SPDLOG_ERROR("can't read process info of {}", pid);
        return;
    }

    proc_mem_virt = kp.ki_size;
    proc_mem_resident = static_cast<uint64_t>(kp.ki_rssize) * getpagesize();
    proc_mem_shared = 0;
}

// No cgroups, the HUD hides the element while this is 0
void update_cgroup_mem()
{
    cgroup_mem_used = 0;
}
