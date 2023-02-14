#include <deque>
#include <mutex>
#include <thread>
#include <chrono>
#include <libdrm/amdgpu.h>
#include "gpu.h"

#include "amdgpu_libdrm.h"

std::deque<struct libdrm_sample> sample_buf(LIBDRM_SAMPLE_BUF_SIZE, {0});
std::mutex sample_buf_m;

void libdrm_do_sample(libdrm_sample *sample) {

}

void libdrm_thread() {
    struct libdrm_sample sample;
    libdrm_do_sample(&sample);
    
    sample_buf_m.lock();
    sample_buf.pop_front();
    sample_buf.push_back(sample);
    sample_buf_m.unlock();

    std::this_thread::sleep_for(std::chrono::milliseconds(LIBDRM_SAMPLE_DELAY));
}

void libdrm_get_info() {
    static bool init = false;
    if (!init) {
        std::thread(libdrm_thread).detach();
        init = true;
    }

    struct libdrm_stats stats;

    sample_buf_m.lock();
    for (auto sample : sample_buf) {
        stats.busy += sample.busy_bit ? 1 : 0; // the ternary is probably not needed
    }
    sample_buf_m.unlock();

    gpu_info.load = (int)(((double)stats.busy / LIBDRM_SAMPLE_BUF_SIZE) * 100);
}