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
            ipc->prune_clients();
            usleep(7000);
        }
}
