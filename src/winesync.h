#include "file_utils.h"
#include <filesystem.h>
#include <string>
#include <spdlog/spdlog.h>

namespace fs = ghc::filesystem;

class WineSync {
    private:
        enum syncMethods {
            NONE,
            WINESERVER,
            ESYNC,
            FSYNC,
            NTSYNC
        };

        int method = 0;
        bool inside_wine = true;

        const char* methods[5] = {
            "NONE",
            "Wserver",
            "Esync",
            "Fsync",
            "NTsync"
        };

    public:
        WineSync() {
#ifdef __linux__
            // check that's were inside wine
            std::string wineProcess = get_exe_path();
            auto n = wineProcess.find_last_of('/');
            std::string preloader = wineProcess.substr(n + 1);
            if (preloader != "wine-preloader" && preloader != "wine64-preloader"){
                inside_wine = false;
                return;
            }

            const char* paths[2] {
                "/proc/self/map_files",
                "/proc/self/fd"
            };

            // check which sync wine is using, if any.
            fs::path path;
            for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
                path = paths[i];
                for (auto& p : fs::directory_iterator(path)) {
                    auto filepath = p.path().string();
                    const char* filename = filepath.c_str();
                    auto sym = read_symlink(filename);
                    if (sym.find("winesync") != std::string::npos)
                    method = syncMethods::NTSYNC;
                    else if (sym.find("fsync") != std::string::npos)
                        method = syncMethods::FSYNC;
                    else if (sym.find("ntsync") != std::string::npos)
                        method = syncMethods::NTSYNC;
                    else if (sym.find("esync") != std::string::npos)
                        method = syncMethods::ESYNC;

                    if (method)
                        break;

                }
                if (method)
                    break;
            }

            SPDLOG_DEBUG("Wine sync method: {}", methods[method]);
#endif
        };

        bool valid() {
            return inside_wine;
        }

        // return sync method as display name
        std::string get_method() {
            return methods[method];
        }
};

extern std::unique_ptr<WineSync> winesync_ptr;