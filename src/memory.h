#include <stdio.h>
#include <thread>

extern pthread_t memoryThread;
extern float memused, memmax;

struct memory_information {
  /* memory information in kilobytes */
  unsigned long long mem, memwithbuffers, memeasyfree, memfree, memmax,
      memdirty;
  unsigned long long swap, swapfree, swapmax;
  unsigned long long bufmem, buffers, cached;
};

void *update_meminfo(void*);
FILE *open_file(const char *file, int *reported);