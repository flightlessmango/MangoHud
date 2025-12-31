#include "socket.h"
const char *path = "/tmp/mangohud.socket";

Socket::Socket() {
    listen_fd = this->make_socket(path);
    sd_event_new(&event);
    sd_event_add_io(event, &listen_src, listen_fd, EPOLLIN, on_accept, this);
    thread = std::thread(&Socket::run, this);
};

void Socket::run() {
    sd_event_loop(event);
}

int Socket::make_socket(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    unlink(path);
    bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(fd, 64);
    return fd;
}

Socket::Client* Socket::find_client_by_fd(int fd) {
    for (auto &p : clients)
        if (p->fd == fd)
            return p.get();
    return nullptr;
}

int Socket::on_accept(sd_event_source *s, int fd, uint32_t revents, void *userdata) {
    (void)s;
    (void)revents;
    auto *self = static_cast<Socket *>(userdata);

    for (;;) {
        int cfd = accept4(fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            return -errno;
        }

        auto c = std::make_unique<Client>();
        c->fd = cfd;

        int r = sd_event_add_io(
            self->event, &c->src, cfd,
            EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR,
            &Socket::on_client_io,
            self   // userdata = this
        );
        if (r < 0) return r;

        self->clients.push_back(std::move(c));
        printf("client connected\n");
    }

    return 0;
}

int Socket::on_client_io(sd_event_source *s, int fd, uint32_t revents, void *userdata) {
    (void)s;
    auto *self = static_cast<Socket *>(userdata);

    Client *c = self->find_client_by_fd(fd);
    if (!c) return 0;

    if (revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        c->dead = true;
        self->prune_dead();
        return 0;
    }

    if (revents & EPOLLIN) {
        for (;;) {
            ssize_t n = recv(fd, c->in.data() + c->have, 4 - c->have, 0);
            if (n > 0) {
                c->have += (size_t)n;

                if (c->have < 4) break;

                uint32_t le_bits;
                memcpy(&le_bits, c->in.data(), 4);
                float value = le_bits_to_float(le_bits);

                c->have = 0;

                c->frametimes[c->n_frames % 200] = value;
                c->n_frames++;
                continue;
            }

            if (n == 0) {
                c->dead = true;
                break;
            }

            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;

            c->dead = true;
            break;
        }

        if (c->dead) self->prune_dead();
    }

    return 0;
}

void Socket::prune_dead() {
    clients.erase(
        std::remove_if(clients.begin(), clients.end(),
            [](auto const& client) { return client->dead; }),
        clients.end());
    printf("client disconnected\n");
}

SocketClient::SocketClient(){
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) { fd = -1; return; }
    sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (::connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno == EINPROGRESS) {
                connecting = true;
                connected = false;
                return;
            }
        close(fd);
        fd = -1;
        return;
    }
}

int SocketClient::flush() {
    if (!check_connected())
        return 0;

    int r = flush_partial();
    if (r != 0) return (r == -EAGAIN) ? 0 : r;

    while (!frames.empty()) {
        partial_buf = frames.front();
        frames.pop_front();
        partial_off = 0;
        partial_active = true;

        r = flush_partial();
        if (r != 0) return (r == -EAGAIN) ? 0 : r;
    }

    return 0;
}

int SocketClient::flush_partial() {
    if (!partial_active) return 0;

    const uint8_t *p = reinterpret_cast<const uint8_t *>(&partial_buf);

    while (partial_off < 4) {
        ssize_t n = ::send(fd, p + partial_off, 4 - partial_off, MSG_NOSIGNAL);
        if (n > 0) {
            partial_off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return -EAGAIN;
        return -errno;
    }

    partial_off = 0;
    partial_active = false;
    return 0;
}

bool SocketClient::check_connected() {
    if (connected) return true;
    if (!connecting) return false;

    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
        return false;

    if (err == 0) {
        connected = true;
        connecting = false;
        return true;
    }

    connecting = false;
    return false;
}
