#pragma once
#ifndef MANGOHUD_IOSTATS_H
#define MANGOHUD_IOSTATS_H

#include <inttypes.h>
#include "timing.hpp"

struct iostats {
    struct {
      unsigned long long read_bytes;
      unsigned long long write_bytes;
    } curr;
    struct {
      unsigned long long read_bytes;
      unsigned long long write_bytes;
    } prev;
    struct {
      float read;
      float write;
    } diff;
    struct {
      float read;
      float write;
    } per_second;
    Clock::time_point last_update;
};

void getIoStats(void *args);

#endif //MANGOHUD_IOSTATS_H
