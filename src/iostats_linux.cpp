#include "iostats.h"
#include "string_utils.h"
#include <fstream>
#include "hud_elements.h"

bool read_io_counters(unsigned long long& read_bytes, unsigned long long& write_bytes) {
    std::string f = "/proc/";

    {
        auto gs_pid = HUDElements.g_gamescopePid;
        f += gs_pid < 1 ? "self" : std::to_string(gs_pid);
        f += "/io";
    }

    std::ifstream file(f);

    if (!file.is_open()) {
        SPDLOG_ERROR("can't open {}", f);
        return false;
    }

    for (std::string line; std::getline(file, line);) {
        if (starts_with(line, "read_bytes:")) {
            try_stoull(read_bytes, line.substr(12));
        }
        else if (starts_with(line, "write_bytes:")) {
            try_stoull(write_bytes, line.substr(13));
        }
    }

    return true;
}
