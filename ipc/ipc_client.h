#include <systemd/sd-bus.h>
#include "proto.h"
#include <deque>

class IPCClient {
public:
    std::atomic<bool> server_up{false};
    std::atomic<bool> need_reconnect{false};
    std::mutex gbm_mtx;
    GbmBuffer gbm{};

    IPCClient() {
        printf("client init\n");
        start();
    }

    bool start();
    void stop();

    void add_to_queue(float& f) {
        {
            std::unique_lock lock(bus_mtx);
            frametimes.push_back(f);
        }
    }

    void drain_queue();
    int push_queue();

    void send_vector(sd_bus_message *m, const std::vector<float> &vec) {
        sd_bus_message_open_container(m, 'a', "d");

        for (float f : vec) {
            double d = static_cast<double>(f);
            sd_bus_message_append(m, "d", d);
        }

        sd_bus_message_close_container(m);
    }



    ~IPCClient() {
        stop();
    }

private:
    static int on_dmabuf_ready(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    void bus_thread();
    bool handshake();
    static int on_server_owner_changed(sd_bus_message* m, void* userdata, sd_bus_error*);
    int install_server_watch();

    std::atomic<bool> quit{false};
    std::thread thread;
    std::deque<float> frametimes;
    std::thread queue_t;

    std::mutex bus_mtx;
    sd_bus* bus = nullptr;
    sd_bus_slot* dmabuf_slot = nullptr;
    sd_bus_slot* server_watch = nullptr;
    int quit_fd = -1;
};
