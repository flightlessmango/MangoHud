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

void update_meminfo(void);
FILE *open_file(const char *file, int *reported);

#endif //MANGOHUD_MEMORY_H
