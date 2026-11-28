#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <spdlog/spdlog.h>
#include "notify.h"

// nt.fd is the kqueue, nt.wd the open config file the vnode filter watches.
// Closing the file removes its event.

bool notify_open(notify_thread& nt)
{
    nt.fd = kqueue();
    if (nt.fd < 0) {
        SPDLOG_ERROR("kqueue failed: {}", strerror(errno));
        return false;
    }
    fcntl(nt.fd, F_SETFD, FD_CLOEXEC);
    return true;
}

bool notify_watch(notify_thread& nt, const char* path)
{
    if (nt.wd >= 0) {
        close(nt.wd);
        nt.wd = -1;
    }

    nt.wd = open(path, O_RDONLY | O_CLOEXEC);
    if (nt.wd < 0)
        return false;

    struct kevent change;
    // EV_CLEAR: report each change once, not for as long as it holds
    EV_SET(&change, nt.wd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
           NOTE_WRITE | NOTE_EXTEND | NOTE_DELETE | NOTE_RENAME, 0, nullptr);

    if (kevent(nt.fd, &change, 1, nullptr, 0, nullptr) < 0) {
        close(nt.wd);
        nt.wd = -1;
        return false;
    }
    return true;
}

unsigned notify_poll(notify_thread& nt)
{
    struct kevent events[4];
    const struct timespec no_wait = { 0, 0 };
    unsigned flags = 0;

    int count = kevent(nt.fd, nullptr, 0, events, 4, &no_wait);
    for (int i = 0; i < count; i++) {
        if (events[i].fflags & (NOTE_WRITE | NOTE_EXTEND))
            flags |= NOTIFY_MODIFIED;
        // an editor that saves through a temporary file removes or renames the original
        if (events[i].fflags & (NOTE_DELETE | NOTE_RENAME))
            flags |= NOTIFY_REMOVED;
    }
    return flags;
}

void notify_close(notify_thread& nt)
{
    if (nt.wd >= 0) {
        close(nt.wd);
        nt.wd = -1;
    }
    if (nt.fd >= 0) {
        close(nt.fd);
        nt.fd = -1;
    }
}
