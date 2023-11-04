#include <sys/stat.h>
#include <thread>
#include <nlohmann/json.hpp>
#include <filesystem.h>
#include <inttypes.h>
#include <mesa/util/os_time.h>
#include <spdlog/spdlog.h>
#include "gpu.h"
#include "hud_elements.h"

using json = nlohmann::json;
namespace fs = ghc::filesystem;

class Intel {
    private:
        bool init = false;
        bool runtime = false;
        bool stop = false;
        struct gpuInfo gpu_info_intel {};
        FILE* fdinfo;
        struct stat stat_buffer;
        std::thread thread;

        FILE* find_fd();
        void intel_gpu_thread();
        uint64_t get_gpu_time();
        void get_fdinfo();

    public:
        Intel() {
            if (stat("/run/pressure-vessel", &stat_buffer) == 0)
                runtime = true;

            fdinfo = find_fd();
            thread = std::thread(&Intel::intel_gpu_thread, this);
        }

        void update() {
            get_fdinfo();
            gpu_info = gpu_info_intel;
        }

        ~Intel(){
            stop = true;
            thread.join();
        }
};

extern std::unique_ptr<Intel> intel;
