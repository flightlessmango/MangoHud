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

extern iostats g_io_stats;
void getIoStats(iostats& io);

// Cumulative bytes read and written by the process, per platform. Leaves the
// values alone and returns false when they can't be read.
bool read_io_counters(unsigned long long& read_bytes, unsigned long long& write_bytes);

#endif //MANGOHUD_IOSTATS_H
