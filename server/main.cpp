#include "server.h"
#include "config.h"
#include "stdio.h"
#include "mesa/os_time.h"

int main() {
  MangoHudServer();
  return 0;
}

void MangoHudServer::loop() {
  while (!stop.load()) {
    if (!ipc->clients.empty()) {
      if (metrics->table) {
        // auto now = os_time_get_nano();
        ipc->queue_all_frames();
        std::deque<frame> done_frames = imgui->drain_queue();
        ipc->notify_frame_ready(std::move(done_frames));
        // printf("duration: %.1f\n", (os_time_get_nano() - now) / 1000000.f);
      }
    }
    usleep(8000);
  };
  printf("server stopping\n");
}
