#include "memory.h"
#include <iomanip>
#include <cstring>
#include <stdio.h>
#include <iostream>

struct memory_information mem_info;

FILE *open_file(const char *file, int *reported) {
  FILE *fp = nullptr;

  fp = fopen(file, "re");

  if (fp == nullptr) {
    if ((reported == nullptr) || *reported == 0) {
      // NORM_ERR("can't open %s: %s", file, strerror(errno));
      if (reported != nullptr) { *reported = 1; }
    }
    return nullptr;
  }

  return fp;
}

void update_meminfo() {
  FILE *meminfo_fp;
  static int reported = 0;

  /* unsigned int a; */
  char buf[256];

  /* With multi-threading, calculations that require
   * multple steps to reach a final result can cause havok
   * if the intermediary calculations are directly assigned to the
   * information struct (they may be read by other functions in the meantime).
   * These variables keep the calculations local to the function and finish off
   * the function by assigning the results to the information struct */
  unsigned long long shmem = 0, sreclaimable = 0, curmem = 0, curbufmem = 0,
                     cureasyfree = 0, memavail = 0;

  mem_info.memmax = mem_info.memdirty = mem_info.swap = mem_info.swapfree = mem_info.swapmax =
      mem_info.memwithbuffers = mem_info.buffers = mem_info.cached = mem_info.memfree =
          mem_info.memeasyfree = 0;

  if (!(meminfo_fp = open_file("/proc/meminfo", &reported))) { }

  while (!feof(meminfo_fp)) {
    if (fgets(buf, 255, meminfo_fp) == nullptr) { break; }

    if (strncmp(buf, "MemTotal:", 9) == 0) {
      sscanf(buf, "%*s %llu", &mem_info.memmax);
    } else if (strncmp(buf, "MemFree:", 8) == 0) {
      sscanf(buf, "%*s %llu", &mem_info.memfree);
    } else if (strncmp(buf, "SwapTotal:", 10) == 0) {
      sscanf(buf, "%*s %llu", &mem_info.swapmax);
    } else if (strncmp(buf, "SwapFree:", 9) == 0) {
      sscanf(buf, "%*s %llu", &mem_info.swapfree);
    } else if (strncmp(buf, "Buffers:", 8) == 0) {
      sscanf(buf, "%*s %llu", &mem_info.buffers);
    } else if (strncmp(buf, "Cached:", 7) == 0) {
      sscanf(buf, "%*s %llu", &mem_info.cached);
    } else if (strncmp(buf, "Dirty:", 6) == 0) {
      sscanf(buf, "%*s %llu", &mem_info.memdirty);
    } else if (strncmp(buf, "MemAvailable:", 13) == 0) {
      sscanf(buf, "%*s %llu", &memavail);
    } else if (strncmp(buf, "Shmem:", 6) == 0) {
      sscanf(buf, "%*s %llu", &shmem);
    } else if (strncmp(buf, "SReclaimable:", 13) == 0) {
      sscanf(buf, "%*s %llu", &sreclaimable);
    }
  }

  curmem = mem_info.memwithbuffers = mem_info.memmax - mem_info.memfree;
  cureasyfree = mem_info.memfree;
  mem_info.swap = mem_info.swapmax - mem_info.swapfree;

  /* Reclaimable memory: does not include shared memory, which is part of cached
     but unreclaimable. Includes the reclaimable part of the Slab cache though.
     Note: when shared memory is swapped out, shmem decreases and swapfree
     decreases - we want this.
  */
  curbufmem = (mem_info.cached - shmem) + mem_info.buffers + sreclaimable;

  curmem = mem_info.memmax - memavail;
  cureasyfree += curbufmem;

  /* Now that we know that every calculation is finished we can wrap up
   * by assigning the values to the information structure */
  mem_info.mem = curmem;
  mem_info.bufmem = curbufmem;
  mem_info.memeasyfree = cureasyfree;
	
  std::cout << (float(mem_info.memmax) - float(mem_info.memeasyfree)) / (1024 * 1024) << " / " << float(mem_info.memmax) / (1024 * 1024) << std::endl;

  fclose(meminfo_fp);
}