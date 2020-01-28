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
using namespace std;

int gpuLoad, gpuTemp, cpuTemp;
string gpuLoadDisplay, cpuTempLocation;
FILE *amdGpuFile, *amdTempFile, *cpuTempFile;

const int NUM_CPU_STATES = 10;

struct Cpus{
  size_t num;
  string name;
  int value;
  string output;
  int freq;
};

size_t numCpuCores = std::thread::hardware_concurrency();
size_t arraySize = numCpuCores + 1;
std::vector<Cpus> cpuArray;
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

void coreCounting(){
  cpuArray.push_back({0, "CPU:"});
  for (size_t i = 0; i < arraySize; i++) {
    size_t offset = i;
    stringstream ss;
    ss << "CPU " << offset << ":";
    string cpuNameString = ss.str();
    cpuArray.push_back({i+1 , cpuNameString});
  }
}

std::string m_cpuUtilizationString;

enum CPUStates
{
	S_USER = 0,
	S_NICE,
	S_SYSTEM,
	S_IDLE,
	S_IOWAIT,
	S_IRQ,
	S_SOFTIRQ,
	S_STEAL,
	S_GUEST,
	S_GUEST_NICE
};

typedef struct CPUData
{
	std::string cpu;
	size_t times[NUM_CPU_STATES];
} CPUData;

void ReadStatsCPU(std::vector<CPUData> & entries)
{
	std::ifstream fileStat("/proc/stat");

	std::string line;

	const std::string STR_CPU("cpu");
	const std::size_t LEN_STR_CPU = STR_CPU.size();
	const std::string STR_TOT("tot");

	while(std::getline(fileStat, line))
	{
		// cpu stats line found
		if(!line.compare(0, LEN_STR_CPU, STR_CPU))
		{
			std::istringstream ss(line);

			// store entry
			entries.emplace_back(CPUData());
			CPUData & entry = entries.back();

			// read cpu label
			ss >> entry.cpu;

			if(entry.cpu.size() > LEN_STR_CPU)
				entry.cpu.erase(0, LEN_STR_CPU);
			else
				entry.cpu = STR_TOT;

			// read times
			for(int i = 0; i < NUM_CPU_STATES; ++i)
				ss >> entry.times[i];
		}
	}
}

size_t GetIdleTime(const CPUData & e)
{
	return	e.times[S_IDLE] +
			e.times[S_IOWAIT];
}

size_t GetActiveTime(const CPUData & e)
{
	return	e.times[S_USER] +
			e.times[S_NICE] +
			e.times[S_SYSTEM] +
			e.times[S_IRQ] +
			e.times[S_SOFTIRQ] +
			e.times[S_STEAL] +
			e.times[S_GUEST] +
			e.times[S_GUEST_NICE];
}

void PrintStats(const std::vector<CPUData> & entries1, const std::vector<CPUData> & entries2)
{
	const size_t NUM_ENTRIES = entries1.size();

	for(size_t i = 0; i < NUM_ENTRIES; ++i)
	{
		const CPUData & e1 = entries1[i];
		const CPUData & e2 = entries2[i];

		const float ACTIVE_TIME	= static_cast<float>(GetActiveTime(e2) - GetActiveTime(e1));
		const float IDLE_TIME	= static_cast<float>(GetIdleTime(e2) - GetIdleTime(e1));
		const float TOTAL_TIME	= ACTIVE_TIME + IDLE_TIME;

    cpuArray[i].value = (truncf(100.f * ACTIVE_TIME / TOTAL_TIME) * 10 / 10);
	}
}

void *cpuInfo(void *){
	FILE *cpuInfo = fopen("/proc/cpuinfo", "r");
    char line[256];
	int i = 0;
    while (fgets(line, sizeof(line), cpuInfo)) {
		std::string row;
		row = line;
		if (row.find("MHz") != std::string::npos){
			row = std::regex_replace(row, std::regex(R"([^0-9.])"), "");
			cpuArray[i + 1].freq = stoi(row);
			i++;
		}
    }

    fclose(cpuInfo);

	char buff[6];
	rewind(cpuTempFile);
    fflush(cpuTempFile);
   	fscanf(cpuTempFile, "%s", buff);
	cpuTemp = stoi(buff) / 1000;
	pthread_detach(cpuInfoThread);
	
	return NULL;
}

void *queryNvidiaSmi(void *){
	vector<string> smiArray;
	string nvidiaSmi = exec("nvidia-smi --query-gpu=utilization.gpu,temperature.gpu --format=csv,noheader | tr -d ' ' | head -n1 | tr -d '%'");
	istringstream f(nvidiaSmi);
	string s;
	while (getline(f, s, ',')) {
        smiArray.push_back(s);
    }
	gpuLoadDisplay = smiArray[0];
	gpuLoad = stoi(smiArray[0]);
	gpuTemp = stoi(smiArray[1]);
	
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

void *getCpuUsage(void *)
{
	std::vector<CPUData> entries1;
	std::vector<CPUData> entries2;

	// snapshot 1
	ReadStatsCPU(entries1);

	// 100ms pause
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	// snapshot 2
	ReadStatsCPU(entries2);

	// print output
	PrintStats(entries1, entries2);
	pthread_detach(cpuThread);
	return NULL;
}


void updateCpuStrings(){
  for (size_t i = 0; i < arraySize; i++) {
    size_t spacing = 10;
    string value = to_string(cpuArray[i].value);
    value.erase( value.find_last_not_of('0') + 1, std::string::npos );
    size_t correctionValue = (spacing - cpuArray[i].name.length()) - value.length();
    string correction = "";
    for (size_t i = 0; i < correctionValue; i++) {
          correction.append(" ");
        }
        stringstream ss;
        if (i < 11) {
          if (i == 0) {
            ss << cpuArray[i].name << " " << cpuArray[i].value << "%";
          } else {
            ss << cpuArray[i].name << correction << cpuArray[i].value << "%";
          }
        } else {
          ss << cpuArray[i].name << correction << cpuArray[i].value << "%";
        }
        cpuArray[i].output = ss.str();
      }
    }