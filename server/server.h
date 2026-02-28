#pragma once
#include <atomic>
#include "ipc.h"
#include "vulkan_ctx.h"
#include "metrics/metrics.h"
#include "config.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

class MangoHudServer {
public:
    std::shared_ptr<Config> config;

    MangoHudServer() {
        auto name = std::string("\x1b[38;2;173;100;193m") + "MANGOHUD" + "\x1b[0m" +
                    " " +
                    "\x1b[38;2;46;151;203m" + std::string("SERVER") + "\x1b[0m";
        logger = spdlog::stderr_color_mt(name);
        spdlog::set_default_logger(logger);
        // TODO Don't force debug
        spdlog::set_level(spdlog::level::debug);

        config  = std::make_shared<Config>();
        ipc     = std::make_unique<IPCServer>(this);
        metrics = std::make_unique<Metrics>(*ipc, config);
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

private:
    std::unique_ptr<IPCServer> ipc;
    std::unique_ptr<Metrics> metrics;
    std::shared_ptr<spdlog::logger> logger;
    std::vector<std::weak_ptr<VkCtx>> vk_contexts;
    std::mutex vk_ctx_m;
    std::atomic<bool> stop {false};

    void loop();
};
