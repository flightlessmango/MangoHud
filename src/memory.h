#pragma once
#ifndef MANGOHUD_MEMORY_H
#define MANGOHUD_MEMORY_H

#include <stdio.h>
#include <thread>

extern float memused, memmax, swapused, swapmax;

struct memory_information {
  /* memory information in kilobytes */
  unsigned long long mem, memwithbuffers, memeasyfree, memfree, memmax,
      memdirty;
  unsigned long long swap, swapfree, swapmax;
  unsigned long long bufmem, buffers, cached;
};

struct process_mem
{
    float virt, resident, shared;
    long int text, data, dirty;
};
extern process_mem proc_mem;

void update_meminfo(void);
void update_procmem();
FILE *open_file(const char *file, int *reported);

#endif //MANGOHUD_MEMORY_H
