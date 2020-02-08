#pragma once
#include <future>
#include <functional>
#include <type_traits>

template<class ret_type, class Fn, class... Args>
inline auto runAsyncAndCatch(Fn&& fn, Args&&... args) {
    auto make_call = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);

    return std::async(std::launch::async, [=]() -> decltype(make_call()) {
        try {
            return make_call();
        } catch (...) {
            // oops, return default inited
            return ret_type();
        }
    });
}
