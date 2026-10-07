#pragma once

#include "../wayland/wayland_ctx.h"
#include "../wayland/wayland.h"
#include "../render/renderer.h"
#include "common/helpers.hpp"
// The protocol uses a C++ keyword for its namespace argument.
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/types.h>

class Client;
class IPCServer;
struct BufferSet;
enum ExportMethod : int32_t;

class Wayland {
public:
    explicit Wayland(IPCServer* ipc, std::string display_name = {});
    ~Wayland();

    bool connected() const {
        return display && layer_surface && render_minor >= 0;
    }

    static int64_t render_device(const std::string& display_name);

    int64_t render_minor = -1;

    const std::string& name() const {
        return display_name;
    }

    void import_dmabuf(std::shared_ptr<Client> client,
                       const std::vector<BufferSet>& buffers,
                       uint32_t width,
                       uint32_t height,
                       ExportMethod method);
    void frame_ready(std::shared_ptr<Client> client, int idx, unique_fd fd,
                     std::shared_ptr<Renderer::Resources> resources);

private:
    struct ImportedBuffer {
        std::shared_ptr<Renderer::Resources> resources;
        wl_buffer_slot slot;
        Wayland* wayland = nullptr;
        std::weak_ptr<Client> client;
    };

    struct PendingFrame {
        int idx = -1;
        unique_fd fd;
    };

    struct ActiveSet {
        ActiveSet(Wayland& wayland, const std::shared_ptr<Client>& client);
        std::weak_ptr<Client> client;
        std::shared_ptr<Renderer::Resources> resources;
        std::vector<std::shared_ptr<ImportedBuffer>> buffers;
        std::deque<PendingFrame> frames;
    };

    void run();
    bool import_resources(const std::shared_ptr<Client>& client);
    void dispatch_events();
    void release_buffer(ImportedBuffer& buffer);
    void present();

    static void on_buffer_release(void* data, wl_buffer* buffer);
    static void on_registry_global(void* data, wl_globals&, wl_registry* registry,
                                   uint32_t name, const char* interface, uint32_t version);
    static void on_layer_configure(void* data, zwlr_layer_surface_v1* surface,
                                   uint32_t serial, uint32_t width, uint32_t height);
    static void on_layer_closed(void* data, zwlr_layer_surface_v1*);
    static void on_preferred_scale(void* data, wp_fractional_scale_v1*, uint32_t scale);

    inline static const wp_fractional_scale_v1_listener scale_listener = {
        .preferred_scale = on_preferred_scale,
    };

    inline static const zwlr_layer_surface_v1_listener layer_listener = {
        .configure = on_layer_configure,
        .closed = on_layer_closed,
    };

    inline static const wl_buffer_listener buffer_listener = {
        .release = Wayland::on_buffer_release,
    };

    std::string display_name;
    IPCServer* ipc;
    std::unique_ptr<ActiveSet> active;
    std::vector<std::shared_ptr<ImportedBuffer>> retired;
    uint64_t frame_seq = 0;
    wl_display* display = nullptr;
    WaylandCtx ctx;
    zwlr_layer_shell_v1* layer_shell = nullptr;
    wl_surface* surface = nullptr;
    wl_shm* shm = nullptr;
    wl_buffer* hidden_buffer = nullptr;
    wp_viewport* viewport = nullptr;
    wp_fractional_scale_v1* fractional_scale = nullptr;
    uint32_t preferred_scale = 120;
    zwlr_layer_surface_v1* layer_surface = nullptr;
    bool configured = false;
    bool hidden = false;
    bool mapped = false;
    std::thread thread;
    std::atomic<bool> quit{false};
    std::mutex frame_m;
};
