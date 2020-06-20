#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>
#include <thread>

#include "mesa/util/os_time.h"

using namespace std;
struct logData{
  double fps;
  int cpu_load;
  int gpu_load;
  int cpu_temp;
  int gpu_temp;
  int gpu_core_clock;
  int gpu_mem_clock;
  float gpu_vram_used;
  float ram_used;

  uint64_t previous;
};

extern string os, cpu, gpu, ram, kernel, driver;
extern bool sysInfoFetched;
extern uint64_t elapsedLog;
extern std::vector<std::string> logFiles;
extern double fps;
extern std::vector<logData> logArray;
extern bool loggingOn;
extern uint64_t log_start, log_end;
extern logData currentLogData;
extern bool logUpdate;

void logging(void *params_void);
void writeFile(string filename);
string exec(string command);
string get_log_suffix(void);
void upload_file(void);
void upload_files(void);
