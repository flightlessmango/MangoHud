#pragma once

#define LIBDRM_SAMPLE_DELAY 5
#define LIBDRM_SAMPLE_BUF_SIZE 256

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

extern std::string dri_device_path;
extern bool do_libdrm_sampling;

void libdrm_do_sample();
void libdrm_thread();
void libdrm_get_info();
int libdrm_initialize();
