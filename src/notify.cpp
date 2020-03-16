#include <unistd.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include "config.h"
#include "notify.h"

pthread_t fileChange;

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

void *fileChanged(void *params_void){
    notify_thread *nt = reinterpret_cast<notify_thread *>(params_void);
    int length, i = 0;
    int fd;
    int wd;
    char buffer[EVENT_BUF_LEN];
    fd = inotify_init();
    wd = inotify_add_watch( fd, nt->params->config_file_path.c_str(), IN_MODIFY);
    while (!nt->quit) {
        length = read( fd, buffer, EVENT_BUF_LEN );
        while (i < length) {
            struct inotify_event *event =
                (struct inotify_event *) &buffer[i];
            i += EVENT_SIZE + event->len;
            if (event->mask & IN_MODIFY) {
                std::lock_guard<std::mutex> lk(nt->mutex);
                parse_overlay_config(nt->params, getenv("MANGOHUD_CONFIG"));
            }
        }
        i = 0;
        printf("File Changed\n");
    }
    return NULL;
}