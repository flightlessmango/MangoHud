#include <memory>
#include <vector>

#include "gpu.h"

class MSM {
    private:
        struct gpuInfo gpu_info_msm {};
        std::vector<FILE*> fdinfo;
        void find_fd();
        uint64_t get_gpu_time();
        void get_fdinfo();

    public:
        MSM() {
            find_fd();
        }

        ~MSM() {
            for (size_t i = 0; i < fdinfo.size(); i++) {
                fclose(fdinfo[i]);
            }
            fdinfo.clear();
        }

        void update() {
            if (!fdinfo.empty())
                get_fdinfo();

            gpu_info = gpu_info_msm;
        }
};

extern std::unique_ptr<MSM> msm;
