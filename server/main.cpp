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
        if (config->maybe_reload_config())
            for (auto client : ipc->clients)
                client->send_config();

        ipc->prune_clients();
        sleep(1);
    }
}
