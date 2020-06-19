#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>
#include <thread>

#include "mesa/util/os_time.h"

using namespace std;
struct logData{
  double fps;
  int cpu;
  int gpu;
  uint64_t previous;
};

extern string os, cpu, gpu, ram, kernel, driver;
extern bool sysInfoFetched;
extern int gpuLoadLog, cpuLoadLog;
extern uint64_t elapsedLog;
extern std::vector<std::string> logFiles;
extern double fps;
extern std::vector<logData> logArray;
extern ofstream out;
extern int num;
extern bool loggingOn;
extern uint64_t log_start, log_end;

void logging(void *params_void);
void writeFile(string filename);
string exec(string command);
string get_current_time(void);
void upload_file(void);
void upload_files(void);