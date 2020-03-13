#include "nvidia_info.h"
#include "memory.h"
#include "gpu.h"

struct gpuInfo gpu_info;
FILE *amdGpuFile = nullptr, *amdTempFile = nullptr, *amdGpuVramTotalFile = nullptr, *amdGpuVramUsedFile = nullptr, *amdGpuCoreClockFile = nullptr, *amdGpuMemoryClockFile = nullptr;
pthread_t cpuThread, gpuThread, cpuInfoThread;

void *getNvidiaGpuInfo(void *){
    if (!nvmlSuccess)
        checkNvidia();

    if (nvmlSuccess){
        getNvidiaInfo();
        gpu_info.load = nvidiaUtilization.gpu;
        gpu_info.temp = nvidiaTemp;
        gpu_info.memoryUsed = nvidiaMemory.used / (1024.f * 1024.f * 1024.f);
        gpu_info.CoreClock = nvidiaCoreClock;
        gpu_info.MemClock = nvidiaMemClock * 2;
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
        gpu_info.load = amdgpu.load;
    }

    if (amdTempFile) {
        rewind(amdTempFile);
        fflush(amdTempFile);
        if (fscanf(amdTempFile, "%d", &amdgpu.temp) != 1)
            amdgpu.temp = 0;
        amdgpu.temp /= 1000;
        gpu_info.temp = amdgpu.temp;
    }

    if (amdGpuVramTotalFile) {
        rewind(amdGpuVramTotalFile);
        fflush(amdGpuVramTotalFile);
        if (fscanf(amdGpuVramTotalFile, "%" PRId64, &value) != 1)
            value = 0;
        amdgpu.memoryTotal = float(value) / (1024 * 1024 * 1024);
        gpu_info.memoryTotal = amdgpu.memoryTotal;
    }

    if (amdGpuVramUsedFile) {
        rewind(amdGpuVramUsedFile);
        fflush(amdGpuVramUsedFile);
        if (fscanf(amdGpuVramUsedFile, "%" PRId64, &value) != 1)
            value = 0;
        amdgpu.memoryUsed = float(value) / (1024 * 1024 * 1024);
        gpu_info.memoryUsed = amdgpu.memoryUsed;
    }

    if (amdGpuCoreClockFile) {
        rewind(amdGpuCoreClockFile);
        fflush(amdGpuCoreClockFile);
        if (fscanf(amdGpuCoreClockFile, "%" PRId64, &value) != 1)
            value = 0;

        amdgpu.CoreClock = value / 1000000;
        gpu_info.CoreClock = amdgpu.CoreClock;
    }

    if (amdGpuMemoryClockFile) {
        rewind(amdGpuMemoryClockFile);
        fflush(amdGpuMemoryClockFile);
        if (fscanf(amdGpuMemoryClockFile, "%" PRId64, &value) != 1)
            value = 0;

        amdgpu.MemClock = value / 1000000;
        gpu_info.MemClock = amdgpu.MemClock;
    }

    pthread_detach(gpuThread);
    return NULL;
}