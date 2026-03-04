#pragma once
#include <atomic>
#include "ipc.h"
#include "vulkan_ctx.h"
#include "metrics/metrics.h"
#include "config.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/cfg/env.h>

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
        spdlog::cfg::load_env_levels();
        config  = std::make_shared<Config>();
        ipc     = std::make_unique<IPCServer>(this);
        metrics = std::make_unique<Metrics>(*ipc, config);
        loop();
    }

    std::shared_ptr<VkCtx> vk() {
        std::lock_guard<std::mutex> lock(vk_ctx_m);

        if (auto ctx = vk_ctx.lock())
            return ctx;

        auto ctx = std::make_shared<VkCtx>();
        vk_ctx = ctx;
        return ctx;
    }

    ~MangoHudServer() {
        stop.store(true);
    }

private:
    std::unique_ptr<IPCServer> ipc;
    std::unique_ptr<Metrics> metrics;
    std::shared_ptr<spdlog::logger> logger;
    std::weak_ptr<VkCtx> vk_ctx;
    std::mutex vk_ctx_m;
    std::atomic<bool> stop {false};

    void loop();
};
