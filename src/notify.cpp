#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>
#include "config.h"
#include "notify.h"

static void fileChanged(notify_thread *nt) {
    overlay_params local_params = *nt->params;

    while (!nt->quit.load()) {
        const unsigned events = notify_poll(*nt);
        if (events) {
            // With NOTIFY_REMOVED, some editors may do a save-to-temp-file/delete-original/move-temp-file
            // so sleep a little to let file to be replaced
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            parse_overlay_config(&local_params, getenv("MANGOHUD_CONFIG"), false);
            if ((events & NOTIFY_REMOVED) || (nt->params->config_file_path != local_params.config_file_path)) {
                SPDLOG_DEBUG("Watching config file: {}", local_params.config_file_path.c_str());
                notify_watch(*nt, local_params.config_file_path.c_str());
            }
            std::lock_guard<std::mutex> lk(nt->mutex);
            *nt->params = local_params;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool start_notifier(notify_thread& nt)
{
    nt.quit.store(false);
    if (!notify_open(nt))
        return false;

    if (!notify_watch(nt, nt.params->config_file_path.c_str())) {
        notify_close(nt);
        return false;
    }

    if (nt.thread.joinable())
        nt.thread.join();
    nt.thread = std::thread(fileChanged, &nt);
    return true;
}

notify_thread::~notify_thread()
{
    quit.store(true);
    if (thread.joinable())
        thread.join();

    notify_close(*this);
}
