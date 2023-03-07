#include <deque>
#include <mutex>
#include <thread>
#include <chrono>
#include <xf86drm.h>
#include <libdrm/amdgpu.h>
#include <spdlog/spdlog.h>
#include <fcntl.h>
#include <unistd.h>
#include "gpu.h"

#include "amdgpu_libdrm.h"

#define LIBDRM_MAX_DEVICES 32

#define LIBDRM_SAMPLE_DELAY 3500
#define LIBDRM_SAMPLE_BUF_SIZE 512

#define LIBDRM_GRBM_STATUS 0x8010

enum LIBDRM_GRBM_BITS {
    LIBDRM_GRBM_BUSY_BIT = 1U << 31
};

struct libdrm_sample {
    bool busy_bit;
};

struct libdrm_stats {
    int busy;
};

std::string dri_device_path;
bool do_libdrm_sampling = false;

std::deque<struct libdrm_sample> sample_buf(LIBDRM_SAMPLE_BUF_SIZE, {0});
std::mutex sample_buf_m;

amdgpu_device_handle amdgpu_handle;

static void libdrm_do_sample(libdrm_sample *sample) {
    uint32_t registers;
    amdgpu_read_mm_registers(amdgpu_handle, LIBDRM_GRBM_STATUS / 4, 1, 0xffffffff, 0, &registers);

    if (registers & LIBDRM_GRBM_BUSY_BIT) sample->busy_bit = true;
}

static void libdrm_thread() {
    while (true) {
        auto start_time = std::chrono::system_clock::now().time_since_epoch();

        struct libdrm_sample sample {0};
        libdrm_do_sample(&sample);
        
        sample_buf_m.lock();
        sample_buf.pop_front();
        sample_buf.push_back(sample);
        sample_buf_m.unlock();

        auto end_time = std::chrono::system_clock::now().time_since_epoch();
        auto sleep_duration = std::chrono::microseconds(LIBDRM_SAMPLE_DELAY) - (end_time - start_time);
        if (sleep_duration > std::chrono::nanoseconds(0)) {
            std::this_thread::sleep_for(sleep_duration);
        }
    }
}

static int libdrm_initialize() {
    drmDevicePtr devices[LIBDRM_MAX_DEVICES];
    int device_count = drmGetDevices2(0, devices, LIBDRM_MAX_DEVICES);
    if (device_count < 0) {
        SPDLOG_ERROR("drmGetDevices2 failed");       
        return -1;
    }

    char *renderd_node = nullptr;
    for (int i = 0; i < device_count; i++) {
        constexpr int required_nodes = (1 << DRM_NODE_PRIMARY) | (1 << DRM_NODE_RENDER);
        if ((devices[i]->available_nodes & required_nodes) != required_nodes) {
            continue;
        }

        if (devices[i]->nodes[DRM_NODE_PRIMARY] == dri_device_path) {
            renderd_node = devices[i]->nodes[DRM_NODE_RENDER];
            break;
        }
    }

    if (renderd_node == nullptr) {
        SPDLOG_ERROR("No renderD node found for '{}'", dri_device_path);
        drmFreeDevices(devices, device_count);
        return -1;
    }

    int fd = open(renderd_node, O_RDWR);
    if (fd < 0) {
        SPDLOG_ERROR("renderD node open failed: '{}'", dri_device_path);
        drmFreeDevices(devices, device_count);
        return -1;
    }

    uint32_t libdrm_minor, libdrm_major;
    if (amdgpu_device_initialize(fd, &libdrm_major, &libdrm_minor, &amdgpu_handle)) {
        SPDLOG_ERROR("amdgpu_device_initialize failed");
        drmFreeDevices(devices, device_count);
        close(fd);
        return -1;
    }

    return 0;
}

void libdrm_get_info() {
    static bool init = false;
    if (!init) {
        if (libdrm_initialize()) {
            do_libdrm_sampling = false;
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
