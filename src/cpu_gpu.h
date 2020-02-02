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
extern "C"
{
	#include "nvidia_info.h"
}
using namespace std;

int gpuLoad, gpuTemp, cpuTemp;
string gpuLoadDisplay, cpuTempLocation;
FILE *amdGpuFile, *amdTempFile, *cpuTempFile;


int numCpuCores = std::thread::hardware_concurrency();
size_t arraySize = numCpuCores + 1;
// std::vector<Cpus> cpuArray;
pthread_t cpuThread, gpuThread, cpuInfoThread, nvidiaSmiThread;

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
	char buff[6];
	rewind(cpuTempFile);
    fflush(cpuTempFile);
   	fscanf(cpuTempFile, "%s", buff);
	cpuTemp = stoi(buff) / 1000;
	pthread_detach(cpuInfoThread);
	
	return NULL;
}

void *getNvidiaGpuInfo(void *){
	#ifdef HAVE_NVML
		if (!nvmlSuccess)
			checkNvidia();

		if (nvmlSuccess){
			getNvidiaInfo();	
			gpuLoad = nvidiaUtilization.gpu;
			gpuLoadDisplay = gpuLoad;
			gpuTemp = nvidiaTemp;
		}
	#endif
	
	pthread_detach(nvidiaSmiThread);
	return NULL;
}

void *getAmdGpuUsage(void *){
	char buff[5];
	rewind(amdGpuFile);
    fflush(amdGpuFile);
   	fscanf(amdGpuFile, "%s", buff);
	gpuLoadDisplay = buff;
	gpuLoad = stoi(buff);	
	
	rewind(amdTempFile);
    fflush(amdTempFile);
	fscanf(amdTempFile, "%s", buff);
	gpuTemp = (stoi(buff) / 1000);

	pthread_detach(gpuThread);
	return NULL;
}
