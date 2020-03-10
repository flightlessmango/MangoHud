#include <thread>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include "nvidia_info.h"

using namespace std;
extern FILE *amdGpuFile, *amdTempFile, *amdGpuVramTotalFile, *amdGpuVramUsedFile, *amdGpuCoreClockFile, *amdGpuMemoryClockFile;
extern pthread_t cpuThread, gpuThread, cpuInfoThread;

struct amdGpu {
    int load;
    int temp;
    float memoryUsed;
    float memoryTotal;
    int MemClock;
    int CoreClock;
};

struct gpuInfo{
    int load;
    int temp;
    float memoryUsed;
    float memoryTotal;
    int MemClock;
    int CoreClock;
};

extern struct amdGpu amdgpu;
extern struct gpuInfo gpu_info;

void *getNvidiaInfo(void*);
void *getNvidiaGpuInfo(void*);
void *getAmdGpuUsage(void*);