#pragma once
#include <atomic>
#include "ipc.h"
#include "vulkan_ctx.h"
#include "imgui_ctx.h"
#include "metrics/metrics.h"

class MangoHudServer {
    public:
        MangoHudServer() {
            vk      = std::make_unique<VulkanContext>();
            imgui   = std::make_unique<ImGuiCtx>(*vk);
            ipc     = std::make_unique<IPCServer>(*vk, *imgui);
            metrics = std::make_unique<Metrics>(*ipc);
            loop();
        }

        ~MangoHudServer() {
            stop.store(true);
        }

        void loop();

    private:
        std::unique_ptr<VulkanContext> vk;
        std::unique_ptr<ImGuiCtx> imgui;
        std::unique_ptr<IPCServer> ipc;
        std::unique_ptr<Metrics> metrics;

        std::atomic<bool> stop {false};
};
