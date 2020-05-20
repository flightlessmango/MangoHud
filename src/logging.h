#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>
#include <thread>

#include "mesa/util/os_time.h"

using namespace std;

string os, cpu, gpu, ram, kernel, driver;
bool sysInfoFetched = false;
int gpuLoadLog = 0, cpuLoadLog = 0, log_period = 0;
int64_t elapsedLog;

struct logData{
  double fps;
  int cpu;
  int gpu;
  uint64_t previous;
};

double fps;
std::vector<logData> logArray;
ofstream out;
const char* log_period_env = std::getenv("LOG_PERIOD");
int num;
bool loggingOn;
uint64_t log_start, log_end;

void writeFile(string filename){
  out.open(filename, ios::out | ios::app);
  out << "os," << "cpu," << "gpu," << "ram," << "kernel," << "driver" << endl;
  out << os << "," << cpu << "," << gpu << "," << ram << "," << kernel << "," << driver << endl;

  for (size_t i = 0; i < logArray.size(); i++)
    out << logArray[i].fps << "," << logArray[i].cpu  << "," << logArray[i].gpu << "," << logArray[i].previous << endl;

  out.close();
  logArray.clear();
}

void logging(void *params_void){
  overlay_params *params = reinterpret_cast<overlay_params *>(params_void);
  time_t now_log = time(0);
  tm *log_time = localtime(&now_log);
  std::ostringstream buffer;
  buffer << std::put_time(log_time, "%Y-%m-%d_%H-%M-%S");
  string date = buffer.str();


  while (loggingOn){
    uint64_t now = os_time_get();
    elapsedLog = now - log_start;
    logArray.push_back({fps, cpuLoadLog, gpuLoadLog, elapsedLog});

    if (params->log_duration && (elapsedLog) >= params->log_duration * 1000000)
      loggingOn = false;
    else
      this_thread::sleep_for(chrono::milliseconds(log_period));
  }

  writeFile(params->output_file + date);
}
