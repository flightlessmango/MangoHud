#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <unistd.h>
#include <unordered_map>

#include <spdlog/spdlog.h>

std::string read_line(const std::string& filename);
bool ends_with(std::string s1, std::string s2, bool ignore_case = false);
uint64_t try_stoull(const std::string& str);

template <typename Container, typename Mutex>
Container snapshot(const Container& source, Mutex& mutex) {
    std::lock_guard lock(mutex);
    return source;
}

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

template <class Range>
class owning_enumerate_view {
public:
    explicit owning_enumerate_view(Range&& r) : r_(std::move(r)) {}

    auto begin() { return enumerate_view<Range>(r_).begin(); }
    auto end() { return enumerate_view<Range>(r_).end(); }

private:
    Range r_;
};

template <class Range>
auto enumerate(Range&& r) requires (!std::is_lvalue_reference_v<Range&&>) {
    return owning_enumerate_view<std::decay_t<Range>>(std::forward<Range>(r));
}

template <class T, class U>
inline bool contains(const std::deque<T>& d, const U& value) {
    return std::find(d.begin(), d.end(), static_cast<T>(value)) != d.end();
}

template <typename Container, typename T>
bool contains(const Container& container, const T& value) {
    return std::find(container.begin(), container.end(), value) != container.end();
}

template <typename T>
bool same_elements(std::vector<T> a, std::vector<T> b) {
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());

    return a == b;
}

template <typename T>
class Collection {
public:
    Collection() = default;

    void add(T value) {
        add(std::make_shared<T>(std::move(value)));
    }

    void add(T value, int key) {
        add(std::make_shared<T>(std::move(value)), key);
    }

    void add(std::shared_ptr<T> value) {
        if (value == nullptr) {
            SPDLOG_ERROR("Collection: cannot add null value");
            return;
        }

        std::lock_guard lock(m);
        if (storage == Storage::Map) {
            SPDLOG_ERROR("Collection: storage type is already map, cannot add value without key");
            return;
        }

        if (std::ranges::find(vec, value) != vec.end())
            return;

        vec.push_back(std::move(value));
        storage = Storage::Vec;
    }

    void add(std::shared_ptr<T> value, int key) {
        if (value == nullptr) {
            SPDLOG_ERROR("Collection: cannot add null value");
            return;
        }

        std::lock_guard lock(m);
        if (storage == Storage::Vec) {
            SPDLOG_ERROR("Collection: storage type is already vector, cannot add key-value pair");
            return;
        }
        map.try_emplace(key, std::move(value));
        storage = Storage::Map;
    }

    template <typename Member, typename Value>
    void remove(Member T::*member, const Value& value) {
        std::lock_guard lock(m);
        if (storage == Storage::Map) {
            SPDLOG_ERROR("Collection: storage type is already map, cannot remove value without key");
            return;
        }
        std::erase_if(vec, [&](const auto& item) {
            return (*item).*member == value;
        });
    }

    void remove(int key) {
        std::lock_guard lock(m);
        if (storage == Storage::Vec) {
            SPDLOG_ERROR("Collection: storage type is already vector, cannot remove key-value pair");
            return;
        }
        map.erase(key);
    }

    template <typename Member, typename Value>
    std::shared_ptr<T> fetch(Member T::*member, const Value& value) {
        std::shared_ptr<T> result = nullptr;

        std::shared_lock lock(m);
        if (storage == Storage::Map) {
            SPDLOG_ERROR("Collection: storage type is already map, cannot fetch value without key");
            return result;
        }

        for (const auto& item : vec)
            if ((*item).*member == value) {
                if (result) {
                    SPDLOG_ERROR("Collection: multiple items found with the same member value");
                    return nullptr;
                }
                result = item;
            }

        return result;
    }

    template <typename Member, typename Value>
    std::shared_ptr<T> fetch_any(Member T::*member, const Value& value) {

        std::shared_lock lock(m);
        if (storage == Storage::Map) {
            SPDLOG_ERROR("Collection: storage type is already map, cannot fetch value without key");
            return nullptr;
        }

        for (const auto& item : vec)
            if ((*item).*member == value)
                return item;

        return nullptr;
    }

    std::shared_ptr<T> fetch(int key) {
        std::shared_lock lock(m);
        if (storage == Storage::Vec) {
            SPDLOG_ERROR("Collection: storage type is already vector, cannot fetch key-value pair");
            return nullptr;
        }

        auto it = map.find(key);
        if (it != map.end())
            return it->second;

        return nullptr;
    }

private:
    std::shared_mutex m;
    std::vector<std::shared_ptr<T>> vec;
    std::unordered_map<int, std::shared_ptr<T>> map;

    enum class Storage { Unset, Vec, Map};
    Storage storage = Storage::Unset;
};
