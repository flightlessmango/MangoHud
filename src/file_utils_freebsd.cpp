#include "file_utils.h"
#include "string_utils.h"
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <link.h>
#include <unistd.h>
#include <climits>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// Everything here works without procfs/linprocfs mounted.

std::string get_exe_path()
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    char buf[PATH_MAX];
    size_t len = sizeof(buf);

    if (sysctl(mib, 4, buf, &len, nullptr, 0) != 0 || len == 0)
        return std::string();
    return std::string(buf, strnlen(buf, len));
}

std::string get_proc_comm()
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>(getpid()) };
    struct kinfo_proc kp;
    size_t len = sizeof(kp);

    if (sysctl(mib, 4, &kp, &len, nullptr, 0) != 0 || len < sizeof(kp))
        return std::string();
    return std::string(kp.ki_comm, strnlen(kp.ki_comm, sizeof(kp.ki_comm)));
}

std::string get_proc_cmdline()
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ARGS, static_cast<int>(getpid()) };
    size_t len = 0;

    if (sysctl(mib, 4, nullptr, &len, nullptr, 0) != 0 || len == 0)
        return std::string();

    std::string buf(len, '\0');
    if (sysctl(mib, 4, &buf[0], &len, nullptr, 0) != 0)
        return std::string();
    buf.resize(len);
    return buf;
}

bool is_wine_loader(const std::string& name)
{
    // Wine here is usually started without the preloader
    return name == "wine" || name == "wine64"
        || name == "wine-preloader" || name == "wine64-preloader";
}

static int lib_loaded_cb(struct dl_phdr_info* info, size_t, void* data)
{
    auto* args = static_cast<std::pair<const std::string*, bool>*>(data);

    if (info->dlpi_name && *info->dlpi_name
        && to_lower(info->dlpi_name).find(*args->first) != std::string::npos) {
        args->second = true;
        return 1; // stop iterating
    }
    return 0;
}

// Mappings of another process, from its VM map
static bool lib_mapped_in(const std::string& lib, pid_t pid)
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, static_cast<int>(pid) };
    size_t len = 0;

    if (sysctl(mib, 4, nullptr, &len, nullptr, 0) != 0 || len == 0)
        return false;

    // the map can grow between the two calls
    len += len / 4 + 16 * sizeof(struct kinfo_vmentry);
    std::vector<char> buf(len);
    if (sysctl(mib, 4, buf.data(), &len, nullptr, 0) != 0)
        return false;

    // entries are packed, their size varies with the path
    const char* end = buf.data() + len;
    for (const char* p = buf.data(); p + sizeof(int) <= end;) {
        const auto* kve = reinterpret_cast<const struct kinfo_vmentry*>(p);
        if (kve->kve_structsize <= 0 || p + kve->kve_structsize > end)
            break;
        if (kve->kve_path[0] && to_lower(kve->kve_path).find(lib) != std::string::npos)
            return true;
        p += kve->kve_structsize;
    }
    return false;
}

bool lib_loaded(const std::string& lib, pid_t pid)
{
    if (pid != -1 && pid != getpid())
        return lib_mapped_in(lib, pid);

    std::pair<const std::string*, bool> args(&lib, false);
    dl_iterate_phdr(lib_loaded_cb, &args);
    return args.second;
}
