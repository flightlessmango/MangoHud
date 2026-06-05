#include "server.h"
#include "config.h"
#include "stdio.h"
#include "mesa/os_time.h"
#include "vulkan_ctx.h"

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

std::shared_ptr<VkCtx> MangoHudServer::vk(int renderer) {
    std::lock_guard lock(vk_ctx_m);

    if (auto ctx = vk_ctx[renderer].lock())
        return ctx;

    auto ctx = std::make_shared<VkCtx>(renderer);
    vk_ctx[renderer] = ctx;
    return ctx;
}

std::vector<std::shared_ptr<GPU>> MangoHudServer::available_gpus() const {
    return metrics->available_gpus();
}
