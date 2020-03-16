#include <unistd.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include "config.h"
#include "notify.h"

pthread_t fileChange;

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

void *fileChanged(void *params_void){
    overlay_params *params = reinterpret_cast<overlay_params *>(params_void);
    int length, i = 0;
    int fd;
    int wd;
    char buffer[EVENT_BUF_LEN];
    fd = inotify_init();
    wd = inotify_add_watch( fd, config_file_path.c_str(), IN_MODIFY);
    length = read( fd, buffer, EVENT_BUF_LEN );
    while (i < length) {
        struct inotify_event *event =
            (struct inotify_event *) &buffer[i];
        i += EVENT_SIZE + event->len;
    }
    printf("File Changed\n");
    return NULL;
}