#include "server.h"
#include "config.h"
#include "stdio.h"
#include "mesa/os_time.h"
#include "egl_ctx.h"
#include "../render/imgui/imgui_ctx.h"

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

std::shared_ptr<VkCtx> MangoHudServer::vk() {
    std::lock_guard lock(vk_ctx_m);

    if (auto ctx = vk_ctx.lock())
        return ctx;

    auto ctx = std::make_shared<VkCtx>();
    vk_ctx = ctx;
    return ctx;
}

std::shared_ptr<EglCtx> MangoHudServer::egl() {
    std::lock_guard lock(egl_ctx_m);

    if (auto ctx = egl_ctx.lock())
        return ctx;

    auto ctx = std::make_shared<EglCtx>();
    egl_ctx = ctx;
    return ctx;
}

std::shared_ptr<ImGuiCtx> MangoHudServer::imgui() {
    std::lock_guard lock(imgui_ctx_m);

    if (auto ctx = imgui_ctx.lock())
        return ctx;

    auto ctx = std::make_shared<ImGuiCtx>();
    imgui_ctx = ctx;
    return ctx;
}
