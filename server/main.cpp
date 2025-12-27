#include "server.h"
#include "config.h"
#include "stdio.h"
#include "egl_window.h"
#include "mesa/os_time.h"

int main() {
  MangoHudServer();
  return 0;
}

void MangoHudServer::loop() {
  while (!stop.load()) {
    if (!ipc->clients.empty()) {
      uint64_t now = os_time_get_nano();
      if (metrics->table) {
        ipc->queue_all_frames();
        imgui->drain_queue();
      }
      // printf("duration: %.1f\n", (os_time_get_nano() - now) / 1000000.f);
    }
    usleep(6000);
  };
}
