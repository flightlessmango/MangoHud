#pragma once
#include <atomic>
#include "ipc.h"
#include "vulkan_ctx.h"
#include "imgui_ctx.h"
#include "metrics/metrics.h"

class MangoHudServer {
    public:
        MangoHudServer(uint32_t w = 500, uint32_t h = 500)
                      : w(w), h(h), table(table) {
            vk      = std::make_unique<VulkanContext>();
            imgui   = std::make_unique<ImGuiCtx>(*vk);
            ipc     = std::make_unique<IPCServer>(*vk, *imgui, table);
            metrics = std::make_unique<Metrics>(*ipc);
            loop();
        }

        ~MangoHudServer() {
            stop.store(true);
        }

        void loop();

    private:
        uint32_t w,h;
        HudTable table;
        std::unique_ptr<VulkanContext> vk;
        std::unique_ptr<ImGuiCtx> imgui;
        std::unique_ptr<IPCServer> ipc;
        std::unique_ptr<Metrics> metrics;

        std::atomic<bool> stop {false};
};
