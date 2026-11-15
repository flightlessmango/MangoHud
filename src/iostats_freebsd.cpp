#include "iostats.h"
#include <sys/types.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <unistd.h>
#include <algorithm>
#include <spdlog/spdlog.h>
#include "hud_elements.h"

// FreeBSD has no per-process byte counters, like /proc/self/io. The closest
// is the resource usage: ru_inblock and ru_oublock count filesystem I/O
// operations (UFS: buffer cache misses and flushes, OpenZFS: reads and writes
// that reach the pool). The byte counts here are an estimate, those counts
// scaled by the I/O size of the filesystem the process works in.
static unsigned long long io_block_size()
{
    static unsigned long long size = 0;

    if (!size) {
        struct statfs sfs;
        if (statfs(".", &sfs) == 0 && sfs.f_iosize > 0)
            size = sfs.f_iosize;
        else if (statfs("/", &sfs) == 0 && sfs.f_iosize > 0)
            size = sfs.f_iosize;
        else
            size = 32768;

        size = std::min<unsigned long long>(std::max<unsigned long long>(size, 4096), 1024 * 1024);
    }

    return size;
}

bool read_io_counters(unsigned long long& read_bytes, unsigned long long& write_bytes)
{
    auto gs_pid = HUDElements.g_gamescopePid;
    const int pid = gs_pid < 1 ? static_cast<int>(getpid()) : static_cast<int>(gs_pid);

    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };
    struct kinfo_proc kp;
    size_t len = sizeof(kp);

    if (sysctl(mib, 4, &kp, &len, nullptr, 0) != 0 || len < sizeof(kp)) {
        SPDLOG_ERROR("can't read process info of {}", pid);
        return false;
    }

    const unsigned long long size = io_block_size();
    read_bytes = static_cast<unsigned long long>(kp.ki_rusage.ru_inblock) * size;
    write_bytes = static_cast<unsigned long long>(kp.ki_rusage.ru_oublock) * size;
    return true;
}
