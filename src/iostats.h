#pragma once
#include <pthread.h>
#include <inttypes.h>

extern pthread_t ioThread;

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

void *getIoStats(void *args);
