#pragma once

#include <string>
#include <cstdint>

std::string read_line(const std::string& filename);
bool ends_with(std::string s1, std::string s2, bool ignore_case = false);
uint64_t try_stoull(const std::string& str);

class unique_fd {
public:
    unique_fd() = default;

    static unique_fd adopt(int fd) {
        unique_fd out;
        out.state = std::make_unique<state_t>(fd);
        return out;
    }

    static unique_fd dup(int fd) {
        int d = ::dup(fd);
        if (d < 0) {
            SPDLOG_ERROR("shared_fd: dup failed {}", strerror(errno));
            throw std::runtime_error("dup failed");
            return {};
        }
        return adopt(d);
    }

    int get() const noexcept {
        return state ? state->fd : -1;
    }

    operator int() const noexcept {
        return get();
    }

    explicit operator bool() const noexcept {
        return get() >= 0;
    }

    void reset() noexcept {
        state.reset();
    }

private:
    struct state_t {
        explicit state_t(int fd_) : fd(fd_) {}
        ~state_t() {
            if (fd >= 0) close(fd);
        }
        int fd = -1;
    };

    std::unique_ptr<state_t> state;
};

