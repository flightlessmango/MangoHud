#ifndef MANGOHUD_TIMING_HPP
#define MANGOHUD_TIMING_HPP
#include <chrono>

#include "mesa/util/os_time.h"

class MesaClock {
public:
    using rep = int64_t;
    using period = std::nano;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<MesaClock>;
    const static bool is_steady = true;
    static time_point now() noexcept {
        return time_point(duration(os_time_get_nano()));
    }
};

using Clock = MesaClock;
#endif //MANGOHUD_TIMING_HPP