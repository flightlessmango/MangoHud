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

struct logData{
  double fps;
  double cpu;
  double gpu;
  double previous;
};

double fps, elapsedLog;
std::vector<logData> logArray;
ofstream out;
const char* log_duration_env = std::getenv("LOG_DURATION");
const char* mangohud_output_env = std::getenv("MANGOHUD_OUTPUT");
const char* log_period_env = std::getenv("LOG_PERIOD");
int duration, num;
bool loggingOn;
uint64_t log_start;

void writeFile(string date){
	out.open(mangohud_output_env + date, ios::out | ios::app);
  out << "os," << "cpu," << "gpu," << "ram," << "kernel," << "driver" << endl;
  out << os << "," << cpu << "," << gpu << "," << ram << "," << kernel << "," << driver << endl;
	for (size_t i = 0; i < logArray.size(); i++) {
     out << logArray[i].fps << "," << logArray[i].cpu  << "," << logArray[i].gpu << endl;
  }
	out.close();
	logArray.clear();
}

void *logging(void *){
  time_t now_log = time(0);
  tm *log_time = localtime(&now_log);
	string date = to_string(log_time->tm_year + 1900) + "-" + to_string(1 + log_time->tm_mon) + "-" + to_string(log_time->tm_mday) + "_" + to_string(1 + log_time->tm_hour) + "-" + to_string(1 + log_time->tm_min) + "-" + to_string(1 + log_time->tm_sec);
  log_start = os_time_get();
  out.open(mangohud_output_env + date, ios::out | ios::app);

	while (loggingOn){
    uint64_t now = os_time_get();
    elapsedLog = (double)(now - log_start);
    out << fps << "," << cpuLoadLog << "," << gpuLoadLog << "," <<  now - log_start << endl;
		// logArray.push_back({fps, cpuLoadLog, gpuLoadLog, 0.0f});

    if ((elapsedLog) >= duration * 1000000 && log_duration_env)
      loggingOn = false;
    
    this_thread::sleep_for(chrono::milliseconds(log_period));
  }
  // writeFile(date);
  out.close();
  return NULL; 
}