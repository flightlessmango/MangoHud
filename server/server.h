#pragma once
#include <atomic>
#include "ipc.h"
#include "vulkan_ctx.h"
#include "metrics/metrics.h"

class MangoHudServer {
public:
    std::unordered_map<uint32_t, std::shared_ptr<VkCtx>> vk_contexts;
    std::mutex vk_ctx_m;

    MangoHudServer() {
        // TODO Don't force debug
        spdlog::set_level(spdlog::level::debug);
        ipc     = std::make_unique<IPCServer>(this);
        metrics = std::make_unique<Metrics>(*ipc);
        loop();
    }

    void queue_frame(clientRes& r, uint32_t renderer) {
        std::lock_guard<std::mutex> lock(vk_ctx_m);
        auto& ctx = vk_contexts[renderer];
        if (!ctx)
            ctx = std::make_unique<VkCtx>(renderer);
        ctx->queue_frame(r);
    }

    ~MangoHudServer() {
        stop.store(true);
    }

    void loop();

private:
    std::unique_ptr<IPCServer> ipc;
    std::unique_ptr<Metrics> metrics;

    std::atomic<bool> stop {false};

    VkCtx* vk(uint32_t id) {
        std::lock_guard<std::mutex> lock(vk_ctx_m);
        auto& ctx = vk_contexts[id];
        if (!ctx)
            ctx = std::make_unique<VkCtx>(id);

        return ctx.get();
    }

    std::deque<clientRes*> drain_all_queues() {
        std::deque<clientRes*> out;
        std::lock_guard<std::mutex> lock(vk_ctx_m);
        for (auto& [render, vk_] : vk_contexts)
            for (auto res : vk_->drain_queue())
                // don't append if we didn't produce a frame anyway
                if (!res->reinit_dmabuf) out.push_back(res);

        return out;
    }

    void prune_vk_contexts() {
        std::lock_guard lock(ipc->clients_mtx);
        std::lock_guard<std::mutex> lock_vk(vk_ctx_m);
        if (ipc->clients.size() == 0) {
            vk_contexts.clear();
            return;
        }

        for (auto& [renderer, vk] : vk_contexts) {
            for (auto& [name, client] : ipc->clients) {
                if (client->renderMinor == renderer)
                    continue;

                vk.reset();
            }
        }
    }
};
