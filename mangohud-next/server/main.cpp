#include "server.h"
#include "config.h"
#include "stdio.h"
#include "mesa/os_time.h"
#include "egl_ctx.h"
#include "vulkan_ctx.h"
#include <cstdlib>
#include <chrono>
#include <utility>

int main() {
  // Prevent the MangoHud client from injecting into the server.
  setenv("DISABLE_MANGOHUD_NEXT", "1", 1);
  unsetenv("MANGOHUD_NEXT");
  unsetenv("LD_PRELOAD");
  MangoHudServer();
  return 0;
}

void MangoHudServer::loop() {
    auto idle_since = std::chrono::steady_clock::now();
    while (true) {
        if (ipc->stopped())
            break;
        if (config->maybe_reload_config()) {
            std::vector<std::shared_ptr<Client>> clients;
            {
                std::lock_guard lock(ipc->clients_mtx);
                clients = ipc->clients;
            }
            for (auto& client : clients)
                client->send_config();
        }

        ipc->prune_clients();
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard lock(ipc->clients_mtx);
            if (!ipc->clients.empty())
                idle_since = now;
            else if (now - idle_since >= std::chrono::seconds(5)) {
                SPDLOG_DEBUG("No clients remain; stopping server");
                break;
            }
        }
        sleep(1);
    }
}

std::shared_ptr<VkCtx> MangoHudServer::vk(int64_t renderer) {
    std::lock_guard lock(vk_ctx_m);

    if (auto ctx = vk_ctx[renderer].lock())
        return ctx;

    auto ctx = std::make_shared<VkCtx>(renderer);
    vk_ctx[renderer] = ctx;
    return ctx;
}

std::shared_ptr<EglCtx> MangoHudServer::egl(int64_t renderer) {
    return std::make_shared<EglCtx>(renderer);
}

std::vector<std::shared_ptr<GPU>> MangoHudServer::available_gpus() const {
    return metrics->available_gpus();
}

std::shared_ptr<Wayland> MangoHudServer::wayland(std::string_view display) {
    if (display.empty())
        return nullptr;

    std::lock_guard lock(waylands_m);
    const std::string display_name(display);
    if (auto it = waylands.find(display_name); it != waylands.end()) {
        if (it->second->connected())
            return it->second;
        waylands.erase(it);
    }

    auto next = std::make_shared<Wayland>(ipc.get(), display_name);
    if (!next->connected())
        return nullptr;

    waylands[display_name] = next;
    return next;
}

std::vector<std::shared_ptr<Client>> MangoHudServer::clients_for_wayland(std::string_view display) {
    std::vector<std::shared_ptr<Client>> out;
    if (display.empty())
        return out;

    const std::string display_name(display);
    std::vector<std::shared_ptr<Client>> clients;
    {
        std::lock_guard lock(ipc->clients_mtx);
        clients = ipc->clients;
    }

    for (auto& client : clients) {
        std::lock_guard lock(client->m);
        if (client->wayland_display == display_name)
            out.push_back(client);
    }

    return out;
}
