#pragma once
#include <systemd/sd-bus.h>
#include <cstdint>
#include <vector>
#include <drm/drm_fourcc.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <memory>
#include <numeric>
#include "../server/config.h"
#include "../render/imgui_ctx.h"
#include "../render/vulkan_ctx.h"
#include "proto.h"
#include "mesa/os_time.h"

class Client {
public:
    pid_t pid;
    GbmBuffer gbm;
    mutable std::mutex m;
    mutable std::shared_ptr<HudTable> table;
    std::vector<float> frametimes;
    uint64_t n_frames = 0;
    uint64_t n_frames_since_update = 0;
    uint64_t last_fps_update = 0;

    Client(std::string name, VulkanContext& vk,
               ImGuiCtx& imgui, pid_t pid,
               uint32_t w, uint32_t h);
    void queue_frame();
    float avg_fps() {
        static double previous_fps = 0;
        uint64_t now = os_time_get_nano();
        auto elapsed = now - last_fps_update;
        if (elapsed >= 500000000) {
            auto fps = 1000000000.0 * n_frames_since_update / elapsed;
            n_frames_since_update = 0;
            last_fps_update = now;
            previous_fps = fps;
            return fps;
        }
        return previous_fps;
    }
    ~Client() {};

private:
    std::string name;
    VulkanContext& vk;
    ImGuiCtx& imgui;
    uint32_t w,h;
    dmabuf buf;
    RenderTarget target;
    VkFormat fmt = VK_FORMAT_B8G8R8A8_SRGB;


    std::shared_ptr<HudTable> get_table() {
        std::unique_lock lock(m);
        return table;
    }
};

static constexpr const char* kBusName = "io.mangohud.gbm";
static constexpr const char* kObjPath = "/io/mangohud/gbm";
static constexpr const char* kIface   = "io.mangohud.gbm1";
static constexpr uint32_t kProtoVersion = 1;

class IPCServer {
public:
    std::unordered_map<std::string, std::unique_ptr<Client>> clients;
    std::mutex clients_mtx;

    IPCServer(VulkanContext& vk, ImGuiCtx& imgui, HudTable& table);

    void queue_all_frames();
    ~IPCServer();

private:
    VulkanContext& vk;
    ImGuiCtx& imgui;
    HudTable& table;
    sd_bus *bus = nullptr;
    sd_bus_slot *slot = nullptr;
    sd_bus_track *track = nullptr;
    std::thread thread;
    std::atomic<bool> stop {false};

    static int track_handler(sd_bus_track *t, void *userdata);
    void maybe_track_sender(sd_bus_message *m);
    static int on_name_owner_changed(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int handshake(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int set_frame_times(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static inline const sd_bus_vtable ipc_vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("Handshake", "s", "u", IPCServer::handshake, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("SetFrameTimes", "ad", "", IPCServer::set_frame_times, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_SIGNAL("DmabufReady", "huuutuut", 0),
        SD_BUS_VTABLE_END
    };
    int init();
    void dbus_thread();
    int emit_dmabuf_to(std::string name, GbmBuffer& gbm);
    static pid_t sender_pid(sd_bus_message* m);
    Client* get_client(sd_bus_message* m);
};
