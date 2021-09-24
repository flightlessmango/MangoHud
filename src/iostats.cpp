#include "iostats.h"
#include "string_utils.h"
#include <fstream>

void getIoStats(void *args) {
    iostats *io = reinterpret_cast<iostats *>(args);
    if (io) {
        Clock::time_point now = Clock::now(); /* ns */
        std::chrono::duration<float> time_diff = now - io->last_update;

        io->prev.read_bytes  = io->curr.read_bytes;
        io->prev.write_bytes = io->curr.write_bytes;

        std::string line;
        std::ifstream f("/proc/self/io");
        while (std::getline(f, line)) {
            if (starts_with(line, "read_bytes:")) {
                try_stoull(io->curr.read_bytes, line.substr(12));
            }
            else if (starts_with(line, "write_bytes:")) {
                try_stoull(io->curr.write_bytes, line.substr(13));
            }
        }

        if (io->last_update.time_since_epoch().count()) {
            io->diff.read  = (io->curr.read_bytes  - io->prev.read_bytes) / (1024.f * 1024.f);
            io->diff.write = (io->curr.write_bytes - io->prev.write_bytes) / (1024.f * 1024.f);

            io->per_second.read = io->diff.read / time_diff.count();
            io->per_second.write = io->diff.write / time_diff.count();
        }

        io->last_update = now;
    }
}
