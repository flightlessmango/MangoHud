#include <deque>
#include <mutex>
#include <thread>
#include <chrono>
#include <libdrm/amdgpu.h>
#include <spdlog/spdlog.h>
#include <fcntl.h>
#include "gpu.h"

#include "amdgpu_libdrm.h"

std::string dri_device_path;

std::deque<struct libdrm_sample> sample_buf(LIBDRM_SAMPLE_BUF_SIZE, {0});
std::mutex sample_buf_m;

amdgpu_device_handle amdgpu_handle;

void libdrm_do_sample(libdrm_sample *sample) {
    uint32_t registers;
    amdgpu_read_mm_registers(amdgpu_handle, LIBDRM_GRBM_STATUS / 4, 1, 0xffffffff, 0, &registers);

    if (registers & LIBDRM_GRBM_BUSY_BIT) sample->busy_bit = true;
}

void libdrm_thread() {
    while (true) {
        struct libdrm_sample sample {0};
        libdrm_do_sample(&sample);
        
        sample_buf_m.lock();
        sample_buf.pop_front();
        sample_buf.push_back(sample);
        sample_buf_m.unlock();

        std::this_thread::sleep_for(std::chrono::milliseconds(LIBDRM_SAMPLE_DELAY));
    }
}

void libdrm_get_info() {
    static bool init = false;
    if (!init) {
        if (libdrm_initialize()) {
            // TODO: handle error
            SPDLOG_ERROR("Could not initialize libdrm");
            return;
        }
        std::thread(libdrm_thread).detach();
        init = true;
        SPDLOG_INFO("Initialized libdrm sampling");
    }

    struct libdrm_stats stats {0};

    sample_buf_m.lock();
    for (auto sample : sample_buf) {
        stats.busy += sample.busy_bit ? 1 : 0; // the ternary is probably not needed
    }
    sample_buf_m.unlock();

    gpu_info.load = (int)(((double)stats.busy / LIBDRM_SAMPLE_BUF_SIZE) * 100);
}

int libdrm_initialize() {
    int fd = open(dri_device_path.c_str(), O_RDWR);
    if (fd < 0) {
        SPDLOG_ERROR("DRI device open failed");       
        return -1;
    }

    uint32_t libdrm_minor, libdrm_major;
    if (amdgpu_device_initialize(fd, &libdrm_major, &libdrm_minor, &amdgpu_handle)) {
        SPDLOG_ERROR("amdgpu_device_initialize failed");
        return -1;
    }

    return 0;
}