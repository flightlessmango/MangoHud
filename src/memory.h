#pragma once
#ifndef MANGOHUD_MEMORY_H
#define MANGOHUD_MEMORY_H

#include <stdio.h>
#include <thread>

extern float memused, memmax, swapused, swapmax, rss;
extern int memclock, membandwidth;

struct memory_information {
  /* memory information in kilobytes */
  unsigned long long mem, memwithbuffers, memeasyfree, memfree, memmax,
      memdirty;
  unsigned long long swap, swapfree, swapmax;
  unsigned long long bufmem, buffers, cached;
};

struct process_mem
{
    int64_t virt, resident, shared;
    int64_t text, data, dirty;
};
extern process_mem proc_mem;

void update_meminfo(void);
void update_procmem();
FILE *open_file(const char *file, int *reported);

#endif //MANGOHUD_MEMORY_H
