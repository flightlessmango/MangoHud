#pragma once
#include <inttypes.h>

#ifdef __gnu_linux__
#include <pthread.h>
extern pthread_t ioThread;
#endif

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
