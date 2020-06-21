#include <iostream>
#include <vector>
#include <fstream>
#include <chrono>
#include <thread>
#include <condition_variable>

#include "timing.hpp"

#include "overlay_params.h"

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

  Clock::duration previous;
};

class Logger {
public:
  Logger(overlay_params* in_params);

  void start_logging();
  void stop_logging();

  void try_log();

  bool is_active() const { return loggingOn; }

  void wait_until_data_valid();
  void notify_data_valid();

  auto last_log_end() const noexcept { return log_end; }
  auto last_log_begin() const noexcept { return log_start; }

  const std::vector<logData>& get_log_data() const noexcept { return logArray; }
  void clear_log_data() noexcept { logArray.clear(); }
private:
  std::vector<logData> logArray;
  Clock::time_point log_start;
  Clock::time_point log_end;
  bool loggingOn;

  std::mutex values_valid_mtx;
  std::condition_variable values_valid_cv;
  bool values_valid;

  overlay_params* params;
};

extern std::unique_ptr<Logger> logger;

extern string os, cpu, gpu, ram, kernel, driver;
extern bool sysInfoFetched;
extern double fps;
extern logData currentLogData;

string exec(string command);
