#include <thread>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include "nvidia_info.h"
#include "memory.h"

using namespace std;

int gpuLoad = 0, gpuTemp = 0, cpuTemp = 0, gpuMemClock, gpuCoreClock;
FILE *amdGpuFile = nullptr, *amdTempFile = nullptr, *cpuTempFile = nullptr, *amdGpuVramTotalFile = nullptr, *amdGpuVramUsedFile = nullptr, *amdGpuCoreClockFile = nullptr, *amdGpuMemoryClockFile = nullptr;
float gpuMemUsed = 0, gpuMemTotal = 0;

int numCpuCores = std::thread::hardware_concurrency();
pthread_t cpuThread, gpuThread, cpuInfoThread;

struct amdGpu {
    int load;
    int temp;
    int64_t memoryUsed;
    int64_t memoryTotal;
    int MemClock;
    int CoreClock;
};

extern struct amdGpu amdgpu;

string exec(string command) {
   char buffer[128];
   string result = "";

   // Open pipe to file
   FILE* pipe = popen(command.c_str(), "r");
   if (!pipe) {
      return "popen failed!";
   }

   // read till end of process:
   while (!feof(pipe)) {

      // use buffer to read and add to result
      if (fgets(buffer, 128, pipe) != NULL)
         result += buffer;
   }

   pclose(pipe);
   return result;
}


void *cpuInfo(void *){
    rewind(cpuTempFile);
    fflush(cpuTempFile);
    if (fscanf(cpuTempFile, "%d", &cpuTemp) != 1)
        cpuTemp = 0;
    cpuTemp /= 1000;
    pthread_detach(cpuInfoThread);

    return NULL;
}

void *getNvidiaGpuInfo(void *){
    if (!nvmlSuccess)
        checkNvidia();

    if (nvmlSuccess){
        getNvidiaInfo();
        gpuLoad = nvidiaUtilization.gpu;
        gpuTemp = nvidiaTemp;
        gpuMemUsed = nvidiaMemory.used / (1024.f * 1024.f * 1024.f);
        gpuCoreClock = nvidiaCoreClock;
        gpuMemClock = nvidiaMemClock * 2;
    }

    pthread_detach(gpuThread);
    return NULL;
}

void *getAmdGpuUsage(void *){
    int64_t value = 0;

    if (amdGpuFile) {
        rewind(amdGpuFile);
        fflush(amdGpuFile);
        if (fscanf(amdGpuFile, "%d", &amdgpu.load) != 1)
            amdgpu.load = 0;
        gpuLoad = amdgpu.load;
    }

    if (amdTempFile) {
        rewind(amdTempFile);
        fflush(amdTempFile);
        if (fscanf(amdTempFile, "%d", &amdgpu.temp) != 1)
            amdgpu.temp = 0;
        amdgpu.temp /= 1000;
        gpuTemp = amdgpu.temp;
    }

    if (amdGpuVramTotalFile) {
        rewind(amdGpuVramTotalFile);
        fflush(amdGpuVramTotalFile);
        if (fscanf(amdGpuVramTotalFile, "%" PRId64, &value) != 1)
            value = 0;
        amdgpu.memoryTotal = value / (1024 * 1024);
        gpuMemTotal = amdgpu.memoryTotal;
    }

    if (amdGpuVramUsedFile) {
        rewind(amdGpuVramUsedFile);
        fflush(amdGpuVramUsedFile);
        if (fscanf(amdGpuVramUsedFile, "%" PRId64, &value) != 1)
            value = 0;
        amdgpu.memoryUsed = value / (1024 * 1024);
        gpuMemUsed = amdgpu.memoryUsed / 1024.f;
    }

    if (amdGpuCoreClockFile) {
        rewind(amdGpuCoreClockFile);
        fflush(amdGpuCoreClockFile);
        if (fscanf(amdGpuCoreClockFile, "%" PRId64, &value) != 1)
            value = 0;

        amdgpu.CoreClock = value / 1000000;
        gpuCoreClock = amdgpu.CoreClock;
    }

    if (amdGpuMemoryClockFile) {
        rewind(amdGpuMemoryClockFile);
        fflush(amdGpuMemoryClockFile);
        if (fscanf(amdGpuMemoryClockFile, "%" PRId64, &value) != 1)
            value = 0;

        amdgpu.MemClock = value / 1000000;
        gpuMemClock = amdgpu.MemClock;
    }

    pthread_detach(gpuThread);
    return NULL;
}
