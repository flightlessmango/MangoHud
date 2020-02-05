#include <cmath>
#include <iomanip>
#include <array>
#include <vector>
#include <algorithm>
#include <iterator>
#include <thread>
#include <sstream>
#include <fstream>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <sstream>
#include <regex>
#include "nvidia_info.h"

using namespace std;

int gpuLoad = 0, gpuTemp = 0, cpuTemp = 0, gpuMemUsed = 0, gpuMemTotal = 0;
FILE *amdGpuFile = nullptr, *amdTempFile = nullptr, *cpuTempFile = nullptr, *amdGpuVramTotalFile = nullptr, *amdGpuVramUsedFile = nullptr;

int numCpuCores = std::thread::hardware_concurrency();
pthread_t cpuThread, gpuThread, cpuInfoThread, nvidiaSmiThread;

struct amdGpu {
    int load;
    int temp;
    int64_t memoryUsed;
    int64_t memoryTotal;
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
    }

    pthread_detach(nvidiaSmiThread);
    return NULL;
}

void *getAmdGpuUsage(void *){
    rewind(amdGpuFile);
    fflush(amdGpuFile);
    if (fscanf(amdGpuFile, "%d", &amdgpu.load) != 1)
        amdgpu.load = 0;
    gpuLoad = amdgpu.load;

    rewind(amdTempFile);
    fflush(amdTempFile);
    if (fscanf(amdTempFile, "%d", &amdgpu.temp) != 1)
        amdgpu.temp = 0;
    amdgpu.temp /= 1000;
    gpuTemp = amdgpu.temp;

    rewind(amdGpuVramTotalFile);
    fflush(amdGpuVramTotalFile);
    if (fscanf(amdGpuVramTotalFile, "%lld", &amdgpu.memoryTotal) != 1)
        amdgpu.memoryTotal = 0;
    amdgpu.memoryTotal /= (1024 * 1024);
    gpuMemTotal = amdgpu.memoryTotal;

    rewind(amdGpuVramUsedFile);
    fflush(amdGpuVramUsedFile);
    if (fscanf(amdGpuVramUsedFile, "%lld", &amdgpu.memoryUsed) != 1)
        amdgpu.memoryUsed = 0;
    amdgpu.memoryUsed /= (1024 * 1024);
    gpuMemUsed = amdgpu.memoryUsed;

    pthread_detach(gpuThread);
    return NULL;
}
