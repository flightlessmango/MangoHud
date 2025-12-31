#include <systemd/sd-bus.h>
#include "proto.h"
#include <deque>
#include "imgui.h"

struct QueueMsg {
  enum class Type { sample, fence } type;
  std::deque<Sample> samples;
  int fence_fd = -1;
};


class IPCClient {
public:
    std::atomic<bool> server_up{false};
    std::atomic<bool> need_reconnect{false};
    std::mutex gbm_mtx;
    GbmBuffer gbm{};
    uint64_t seq = 0;
    ImVec2 window_size = {500, 500};
    std::atomic<bool> have_new_frame{false};

    IPCClient() {
        thread = std::thread(&IPCClient::bus_thread, this);
    }

    bool init();
    void stop();

    void add_to_queue(uint64_t& now) {
        {
            std::unique_lock lock(samples_mtx);
            samples.push_back({seq, now});
            seq++;
        }
    }

    void drain_queue();
    int push_queue();
    int take_pending_acquire_fd() {
        std::lock_guard<std::mutex> lock(bus_mtx);
        int fd = pending_acquire_fd;
        pending_acquire_fd = -1;
        return fd;
    }
    int send_acquire_fence(int fd);

    ~IPCClient() {
        stop();
    }

private:
    static int on_dmabuf_ready(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int on_fence_ready(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    void bus_thread();
    bool handshake();
    static int on_server_owner_changed(sd_bus_message* m, void* userdata, sd_bus_error*);
    int install_server_watch();

    std::atomic<bool> quit{false};
    std::thread thread;
    std::deque<Sample> samples;
    std::mutex samples_mtx;
    std::deque<int> fences;
    std::mutex fences_mtx;

    std::mutex bus_mtx;
    sd_bus* bus = nullptr;
    sd_bus_slot* dmabuf_slot = nullptr;
    sd_bus_slot* server_watch = nullptr;
    int pending_acquire_fd = -1;
};
