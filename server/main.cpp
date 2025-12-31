#include "server.h"
#include "config.h"
#include "stdio.h"
#include "mesa/os_time.h"

int main() {
  MangoHudServer();
  return 0;
}

void MangoHudServer::loop() {
    // uint64_t acc_work_ns = 0;
    // uint64_t acc_frames = 0;
    // uint64_t last_report_ns = os_time_get_nano();
    std::deque<clientRes *> frames;

    while (!stop.load()) {
        auto start = os_time_get_nano();
        if (metrics && metrics->table) {
            {
                std::lock_guard lock(ipc->clients_mtx);
                for (auto& [name, client] : ipc->clients) {
                    if (client->ready_frame()) {
                        auto& r = client->resources;
                        std::lock_guard lock_r(r.m);
                        if (r.reinit_dmabuf) {
                            r.reset();
                            continue;
                        }

                        queue_frame(r, client->renderMinor);
                        if (r.send_dmabuf)
                            ipc->queue_dmabuf(&r);

                        if (get_cfg() && get_cfg()->config_changed.load()) {
                            ipc->queue_configs();
                            get_cfg()->config_changed.store(false);
                        }
                    }
                }
            }

            // TODO we need to hammer out the sync logic more
            // there's a mix of mutex, fence and semaphores when we
            // probably only need a semaphore per client
            frames = drain_all_queues();
            ipc->notify_frame_ready(frames);

            prune_vk_contexts();
        }

        const uint64_t work_ns = os_time_get_nano() - start;

        // acc_work_ns += work_ns;
        // acc_frames += frames.size();

        // const uint64_t now_ns = os_time_get_nano();
        // if (now_ns - last_report_ns >= 2000'000'000ULL) {
        //     const double avg_ms = acc_frames ? (acc_work_ns / 1'000'000.0) / static_cast<double>(acc_frames) : 0.0;
        //     SPDLOG_DEBUG("avg work: {:.3f} ms ({} frames / 2s)", avg_ms, acc_frames);

        //     acc_work_ns = 0;
        //     acc_frames = 0;
        //     last_report_ns = now_ns;
        // }

        const int64_t sleep_us = 16000 - static_cast<int64_t>(work_ns / 1000);
        if (sleep_us > 0)
            usleep(static_cast<useconds_t>(sleep_us));
    }
}
