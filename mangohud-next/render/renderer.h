#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "../server/common/helpers.hpp"
#include "shared.h"

class EglCtx;
class Client;
class MangoHudServer;
class VkCtx;

enum ExportMethod : int32_t {
    DMABUF_VULKAN = 0,
    OPAQUE_VULKAN,
    DMABUF_EGL,
    COUNT,
};

class Renderer {
public:
    Renderer(MangoHudServer* server, Client* client, int64_t render_minor, int buffer_size);
    ~Renderer();

    static constexpr ExportMethod default_method = DMABUF_VULKAN;

    bool method_failed();
    struct Resources;
    void frame_ready(int idx, unique_fd fd, std::shared_ptr<Resources> source = {});
    void set_hud(HudConfig next);

    struct Resources {
        MangoHudServer* server = nullptr;
        ExportMethod method = COUNT;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<BufferSet> buffers;
        std::vector<unique_fd> render_fences;
        VkCommandPool cmd_pool = VK_NULL_HANDLE;
        std::shared_ptr<VkCtx> vk;
        std::shared_ptr<EglCtx> egl;

        Resources(MangoHudServer* server, ExportMethod method);
        ~Resources();

        bool init_method(int64_t render_minor, int buffer_size, uint32_t w, uint32_t h);

        int dmabuf_fd(int idx) const {
            return buffers[idx].dmabuf.gbm.fd.get();
        }
    };

    std::shared_ptr<Resources> get_resources();

private:
    MangoHudServer* server = nullptr;
    Client* client = nullptr;
    int64_t render_minor = -1;
    int buffer_size = 0;
    std::mutex m;
    uint32_t w = 500;
    uint32_t h = 500;
    std::mutex hud_m;
    std::shared_ptr<HudConfig> hud = std::make_shared<HudConfig>();
    std::shared_ptr<Resources> resources;
    std::atomic<bool> stop{false};
    std::thread thread;
    std::mutex frame_m;
    std::condition_variable frame_cv;
    struct QueuedFrame {
        ready_frame frame;
        std::shared_ptr<Resources> source;
    };
    std::deque<QueuedFrame> frame_queue;

    bool apply_method(ExportMethod method);
    bool apply_method_or_next(ExportMethod method);
    bool resize(const Resolution& size, ExportMethod method);
    void run();
    void process_frame(QueuedFrame queued);
    void send_dmabuf();
};
