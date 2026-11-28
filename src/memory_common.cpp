#include "memory.h"

float memused, memmax, swapused;
int mem_temp = -1;
uint64_t proc_mem_resident, proc_mem_shared, proc_mem_virt;
uint64_t cgroup_mem_used;
