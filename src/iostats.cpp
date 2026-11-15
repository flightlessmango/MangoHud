#include "iostats.h"

struct iostats g_io_stats;

void getIoStats(iostats& io) {
    Clock::time_point now = Clock::now(); /* ns */
    std::chrono::duration<float> time_diff = now - io.last_update;

    io.prev.read_bytes  = io.curr.read_bytes;
    io.prev.write_bytes = io.curr.write_bytes;

    if (!read_io_counters(io.curr.read_bytes, io.curr.write_bytes))
        return;

    if (io.last_update.time_since_epoch().count()) {
        io.diff.read  = (io.curr.read_bytes  - io.prev.read_bytes) / (1024.f * 1024.f);
        io.diff.write = (io.curr.write_bytes - io.prev.write_bytes) / (1024.f * 1024.f);

        io.per_second.read = io.diff.read / time_diff.count();
        io.per_second.write = io.diff.write / time_diff.count();
    }

    io.last_update = now;
}
