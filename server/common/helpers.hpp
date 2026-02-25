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

template <class Range>
class enumerate_view {
public:
    explicit enumerate_view(Range& r) : r_(r) {}

    class iterator {
    public:
        using base_iter = decltype(std::begin(std::declval<Range&>()));

        iterator(std::size_t i, base_iter it) : i_(i), it_(it) {}

        auto operator*() const {
            return std::tuple<std::size_t, decltype(*it_)>(i_, *it_);
        }

        iterator& operator++() {
            ++i_;
            ++it_;
            return *this;
        }

        bool operator!=(const iterator& other) const {
            return it_ != other.it_;
        }

    private:
        std::size_t i_;
        base_iter it_;
    };

    iterator begin() { return iterator(0, std::begin(r_)); }
    iterator end() { return iterator(0, std::end(r_)); }

private:
    Range& r_;
};

template <class Range>
auto enumerate(Range& r) {
    return enumerate_view<Range>(r);
}

template <class T, class U>
inline bool contains(const std::deque<T>& d, const U& value) {
    return std::find(d.begin(), d.end(), static_cast<T>(value)) != d.end();
}
