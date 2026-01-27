#pragma once
#include <atomic>
#include "ipc.h"
#include "vulkan_ctx.h"
#include "metrics/metrics.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

class MangoHudServer {
public:
    std::vector<std::weak_ptr<VkCtx>> vk_contexts;
    std::mutex vk_ctx_m;
    std::shared_ptr<spdlog::logger> logger;

    MangoHudServer() {
        // TODO Don't force debug
        logger = spdlog::stderr_color_mt("MANGOHUD");
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug);
        ipc     = std::make_unique<IPCServer>(this);
        metrics = std::make_unique<Metrics>(*ipc);
        loop();
    }

    std::shared_ptr<VkCtx> vk(uint32_t id) {
        std::lock_guard<std::mutex> lock(vk_ctx_m);

        for (auto it = vk_contexts.begin(); it != vk_contexts.end(); ) {
            if (it->expired()) {
                it = vk_contexts.erase(it);
                continue;
            }

            auto ctx = it->lock();
            if (ctx && ctx->renderMinor == id)
                return ctx;

            ++it;
        }

        auto ctx = std::make_shared<VkCtx>(id);
        vk_contexts.push_back(ctx);
        return ctx;
    }

    ~MangoHudServer() {
        stop.store(true);
    }

    void loop();

private:
    std::unique_ptr<IPCServer> ipc;
    std::unique_ptr<Metrics> metrics;
    std::atomic<bool> stop {false};
};
