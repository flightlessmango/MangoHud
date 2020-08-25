#pragma once
#ifndef MANGOHUD_IOSTATS_H
#define MANGOHUD_IOSTATS_H

#include <pthread.h>
#include <inttypes.h>

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
};

void getIoStats(void *args);

#endif //MANGOHUD_IOSTATS_H
