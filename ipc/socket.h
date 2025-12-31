#pragma once
#include "systemd/sd-event.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <memory>
#include <bit>
#include <byteswap.h>
#include <cstdint>
#include <unistd.h>
#include <deque>
#include <algorithm>
#include <thread>
#include <vector>
#include <array>


static inline uint32_t host_to_le32(uint32_t x) {
    if constexpr (std::endian::native == std::endian::little) return x;
    return bswap_32(x);
}

static inline uint32_t le32_to_host(uint32_t x) {
    if constexpr (std::endian::native == std::endian::little) return x;
    return bswap_32(x);
}

static inline float le_bits_to_float(uint32_t le_bits) {
    uint32_t bits = le32_to_host(le_bits);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

static inline uint32_t float_to_le_bits(float f) {
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return host_to_le32(bits);
}

class Socket {
    public:
        struct Client {
            int fd = -1;
            sd_event_source *src = nullptr;
            bool dead = false;

            std::array<uint8_t, 4> in{};
            size_t have = 0;

            std::vector<float> frametimes;
            Client() : frametimes(200, 0.0f) {};
            uint64_t n_frames = 0;

            ~Client() {
                if (src) sd_event_source_unref(src);
                if (fd >= 0) close(fd);
            }
        };

        std::vector<std::unique_ptr<Client>> clients;
        Socket();

    private:
        sd_event *event;
        int listen_fd;
        sd_event_source *listen_src;
        const char *sock_path;
        std::thread thread;

        void run();
        static int make_socket(const char *path);
        Client* find_client_by_fd(int fd);
        static int on_accept(sd_event_source *s, int fd, uint32_t revents, void *userdata);
        static int on_client_io(sd_event_source *s, int fd, uint32_t revents, void *userdata);
        void prune_dead();
};

class SocketClient {
public:
    SocketClient();
    ~SocketClient() {
        if (fd >= 0) close(fd);
    }

    bool ok() const { return fd >= 0; }
    int push(float v) {
        frames.push_back(float_to_le_bits(v));
        return flush();
    }

    size_t queued() const { return frames.size() + (partial_off ? 1 : 0); }
    uint64_t dropped_samples() const { return dropped; }

private:
    int fd = -1;
    bool connecting = false;
    bool connected = false;

    std::deque<uint32_t> frames;
    size_t max_queue = 4096;
    uint64_t dropped = 0;

    uint32_t partial_buf = 0;
    size_t partial_off = 0;
    bool partial_active = false;

    int flush();
    int flush_partial();
    bool check_connected();
};
