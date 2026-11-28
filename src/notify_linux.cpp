#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <spdlog/spdlog.h>
#include "notify.h"

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

bool notify_open(notify_thread& nt)
{
    nt.fd = inotify_init1(IN_NONBLOCK);
    if (nt.fd < 0) {
        SPDLOG_ERROR("inotify_init1 failed: {}", strerror(errno));
        return false;
    }
    return true;
}

bool notify_watch(notify_thread& nt, const char* path)
{
    if (nt.wd >= 0)
        inotify_rm_watch(nt.fd, nt.wd);
    nt.wd = inotify_add_watch(nt.fd, path, IN_MODIFY | IN_DELETE_SELF);
    return nt.wd >= 0;
}

unsigned notify_poll(notify_thread& nt)
{
    char buffer[EVENT_BUF_LEN];
    unsigned events = 0;

    int length = read(nt.fd, buffer, EVENT_BUF_LEN);
    for (int i = 0; i < length;) {
        struct inotify_event *event = (struct inotify_event *) &buffer[i];
        i += EVENT_SIZE + event->len;
        if (event->mask & IN_MODIFY)
            events |= NOTIFY_MODIFIED;
        if (event->mask & IN_DELETE_SELF)
            events |= NOTIFY_REMOVED;
    }
    return events;
}

void notify_close(notify_thread& nt)
{
    if (nt.fd < 0)
        return;

    inotify_rm_watch(nt.fd, nt.wd);
    close(nt.fd);
    nt.fd = -1;
}
