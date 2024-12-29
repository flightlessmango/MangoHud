#pragma once
#ifndef MANGOHUD_MEMORY_H
#define MANGOHUD_MEMORY_H

#include <cstdint>

extern float memused, memmax, swapused;
extern uint64_t proc_mem_resident, proc_mem_shared, proc_mem_virt;

void update_meminfo();
void update_procmem();

#endif //MANGOHUD_MEMORY_H
