#include "iostats.h"
#include "string_utils.h"
#include "timing.hpp"
#include <fstream>

Clock::time_point lastUpdate = Clock::now(); /* ns */

void getIoStats(void *args) {
    iostats *io = reinterpret_cast<iostats *>(args);
    if (io) {
        Clock::time_point now = Clock::now(); /* ns */
        Clock::duration timeDiff = now - lastUpdate;
        float timeDiffSecs = (float)timeDiff.count() / (float)1000000000; // Time diff in seconds

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

        io->diff.read  = (io->curr.read_bytes  - io->prev.read_bytes) / (1024.f * 1024.f);
        io->diff.write = (io->curr.write_bytes - io->prev.write_bytes) / (1024.f * 1024.f);

        io->per_second.read = io->diff.read / timeDiffSecs;
        io->per_second.write = io->diff.write / timeDiffSecs;

        lastUpdate = now;
    }
}
