#pragma once
#include <cstdint>
#include <mutex>
#include <deque>
#include <string>
#include <memory>
#include <thread>
#include <functional>
#include <queue>
#include <memory>
#include <future>
#include <vector>
#include "../render/renderer.h"
#include "protocol.h"
#include <poll.h>
#include <systemd/sd-bus.h>
#include <sys/eventfd.h>

constexpr size_t   FT_MAX = 200;
constexpr uint64_t KEEP_NS = 500000000ULL;

enum class SampleType : uint8_t {
    Frame,
    Refresh,
    Hud,
    App,
    Count,
};

enum class OutputMode : uint8_t {
    App,
    Layer,
};

struct Sample {
    SampleType type = SampleType::Frame;
    uint64_t seq;
    uint64_t t_ns;
};

struct SampleStats {
    mutable std::mutex m;
    std::deque<Sample> samples;
    std::vector<float> frametimes = std::vector<float>(FT_MAX, 0.0f);
    uint64_t n_samples = 0;
    uint64_t last_fps_update = 0;
    uint64_t seq_last = 0, t_last = 0;
    uint64_t dropped = 0;
    double previous_fps = 0;
    bool have_prev = false;

    void add_sample(SampleType type, uint64_t seq, uint64_t t_ns) {
        std::lock_guard lock(m);
        if (have_prev) {
            if (seq <= seq_last || t_ns <= t_last)
                return;

            uint64_t dt_ns = t_ns - t_last;
            uint64_t dseq  = seq - seq_last;

            if (dseq > 1)
                dropped += (dseq - 1);

            double ft_ms = (double)dt_ns / (double)dseq / 1e6;
            frametimes.push_back(ft_ms);
            if (frametimes.size() > FT_MAX)
                frametimes.erase(frametimes.begin());
        } else {
            have_prev = true;
        }

        samples.push_back({type, seq, t_ns});
        while (samples.size() > 2 && (t_ns - samples.front().t_ns) > KEEP_NS)
            samples.pop_front();

        t_last = t_ns;
        seq_last = seq;
        n_samples++;
    }

    float avg_fps() {
        std::lock_guard lock(m);
        if (samples.size() < 2) return previous_fps;

        const auto& b = samples.back();

        if (last_fps_update != 0 &&
            (b.t_ns - last_fps_update) < 500000000ULL) {
            return previous_fps;
        }

        const auto& a = samples.front();
        uint64_t dseq = b.seq - a.seq;
        uint64_t dt   = b.t_ns - a.t_ns;
        if (dseq == 0 || dt == 0) return previous_fps;

        previous_fps = (float)(1e9 * (double)dseq / (double)dt);
        last_fps_update = b.t_ns;
        return previous_fps;
    }

    float avg_frametime() {
        float fps = avg_fps();
        return fps > 0 ? 1000.f / fps : 0.f;
    }

    std::vector<float> frametimes_copy() const {
        std::lock_guard lock(m);
        return frametimes;
    }
};

class IPCServer;
class MangoHudServer;
class Wayland;

class Client {
public:
    pid_t pid;
    std::mutex m;
    std::vector<SampleStats> samples{static_cast<size_t>(SampleType::Count)};
    std::string name;
    std::string pEngineName;
    std::string vulkanDriver;
    std::string gpuName;
    uint32_t resolutionWidth = 0;
    uint32_t resolutionHeight = 0;
    std::vector<std::string> focused_seats;
    bool x11_focused = false;
    std::string wayland_display;
    OutputMode output_mode = OutputMode::App;
    std::unique_ptr<Renderer> renderer;
    std::shared_ptr<Wayland> wayland;
    IPCServer* ipc;
    MangoHudServer* server;
    sd_bus* bus;
    sd_bus_slot* slot;
    std::atomic<bool> active {true};
    std::atomic<uint64_t> hud_seq{0};
    std::atomic<bool> stop {false};

    Client(pid_t pid_, IPCServer* ipc_, MangoHudServer* server_, sd_bus* bus_);

    bool focused() const {
        return x11_focused || !focused_seats.empty();
    }

    SampleStats& stats_for(SampleType type) {
        auto idx = static_cast<size_t>(type);
        if (idx >= samples.size())
            idx = static_cast<size_t>(SampleType::Frame);
        return samples[idx];
    }

    void init(std::shared_ptr<Client>& shared);
    void send_dmabuf(const std::vector<BufferSet>& buffers, uint32_t width, uint32_t height,
                     ExportMethod method);
    void send_config();
    static int on_connect(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    void set_dead();
    void frame_ready(int idx, unique_fd fd, std::shared_ptr<Renderer::Resources> resources);
    void stop_and_join();

    ~Client();

private:
    std::thread thread;
    sd_bus_slot* handshake_slot = nullptr;
    sd_bus_slot* frame_samples_slot = nullptr;
    sd_bus_slot* resolution_slot = nullptr;
    sd_bus_slot* spdlog_slot = nullptr;
    sd_bus_slot* frame_slot = nullptr;
    sd_bus_slot* import_failed_slot = nullptr;
    sd_event* event = nullptr;
    sd_event_source* stop_src = nullptr;
    sd_event_source* work_src = nullptr;
    int stop_eventfd = -1;
    int work_eventfd = -1;
    std::mutex work_mtx;
    std::queue<std::packaged_task<void()>> work_q;
    std::shared_ptr<spdlog::logger> logger;
    std::weak_ptr<Client> self_weak;

    void dbus_thread();
    void send_dmabuf_ipc(const std::vector<BufferSet>& buffers, uint32_t width, uint32_t height,
                         ExportMethod method);
    void send_dmabuf_wayland(const std::vector<BufferSet>& buffers, uint32_t width, uint32_t height,
                             ExportMethod method);
    std::shared_ptr<Wayland> ensure_wayland();
    void setup_handshake(std::string member, sd_bus_slot** slot,
                         sd_bus_message_handler_t callback, std::shared_ptr<Client>& shared);

    static int frame_samples(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int resolution(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int spdlog_msg(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int on_frame(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int on_import_failed(sd_bus_message* m, void* userdata, sd_bus_error*);
    static int on_bus_disconnected(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    static int on_stop_event(sd_event_source *s, int fd, uint32_t revents, void *userdata);
    static int on_work_event(sd_event_source *s, int fd, uint32_t revents, void *userdata);
    template <class F>
    void post(F&& fn) {
        {
            std::lock_guard<std::mutex> lock(work_mtx);
            work_q.emplace(std::forward<F>(fn));
        }

        uint64_t one = 1;
        ssize_t n = write(work_eventfd, &one, sizeof(one));
        (void)n;
    }
    void wait_on_fences();
};
