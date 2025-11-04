#include "file_utils.h"
#include <filesystem.h>
#include <string>
#include <spdlog/spdlog.h>
#include "hud_elements.h"
#include "overlay.h"

namespace fs = ghc::filesystem;
class WineSync {
    private:
        const std::unordered_map<std::string, std::string> methods {
            {"NONE", "NONE"},
            {"winesync", "Wserver"},
            {"esync", "Esync"},
            {"fsync", "Fsync"},
            {"ntsync", "NTsync"},
        };

        pid_t pid;
        std::string method = "NONE";
        bool inside_wine = true;
    public:
        void determine_sync_variant() {
#ifdef __linux__
            // check that's were inside wine
            std::string wineProcess = get_exe_path();
            auto n = wineProcess.find_last_of('/');
            std::string preloader = wineProcess.substr(n + 1);
            if (preloader != "wine-preloader" && preloader != "wine64-preloader"){
                inside_wine = false;
                return;
            }

            for (auto& p : methods) {
                if (lib_loaded(p.first, pid)) {
                    method = p.first;
                    break;
                }
            }

            SPDLOG_DEBUG("Wine sync method: {}", methods.at(method));
#endif
        }

        bool valid() {
            return inside_wine;
        }

        // return sync method as display name
        const char* get_method() {
            return methods.at(method).c_str();
        }

        void set_pid(pid_t _pid) {
            if (_pid != pid) {
                pid = _pid;
                determine_sync_variant();
            }
        }
};

extern std::unique_ptr<WineSync> winesync_ptr;
