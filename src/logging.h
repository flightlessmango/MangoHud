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
logData currentLogData = {};


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

void upload_file(){
  std::string command = "curl --include --request POST https://flightlessmango.com/logs -F 'log[game_id]=26506' -F 'log[user_id]=176' -F 'attachment=true' -A 'mangohud' ";
  command += " -F 'log[uploads][]=@" + logFiles.back() + "'";
  
  command += " | grep Location | cut -c11-";
  std::string url = exec(command);
  exec("xdg-open " + url);
}

void upload_files(){
  std::string command = "curl --include --request POST https://flightlessmango.com/logs -F 'log[game_id]=26506' -F 'log[user_id]=176' -F 'attachment=true' -A 'mangohud' ";
  for (auto& file : logFiles)
    command += " -F 'log[uploads][]=@" + file + "'";
  
  command += " | grep Location | cut -c11-";
  std::string url = exec(command);
  exec("xdg-open " + url);
}

void writeFile(string filename, overlay_params* params){
  logFiles.push_back(filename);
  out.open(filename, ios::out | ios::app);
  out << "os," << "cpu," << "gpu," << "ram," << "kernel," << "driver" << endl;
  out << os << "," << cpu << "," << gpu << "," << ram << "," << kernel << "," << driver << endl;

  for (size_t i = 0; i < logArray.size(); i++){
    out << logArray[i].fps << ",";
    out << logArray[i].cpu_load << ",";
    out << logArray[i].gpu_load << ",";
    out << logArray[i].cpu_temp << ",";
    out << logArray[i].gpu_temp << ",";
    out << logArray[i].gpu_core_clock << ",";
    out << logArray[i].gpu_mem_clock << ",";
    out << logArray[i].previous << "\n";
  }

  out.close();
  logArray.clear();
}

string get_current_time(){
  time_t now_log = time(0);
  tm *log_time = localtime(&now_log);
  std::ostringstream buffer;
  buffer << std::put_time(log_time, "%Y-%m-%d_%H-%M-%S");
  string date = buffer.str();
  return date;
}

void logging(void *params_void){
  overlay_params *params = reinterpret_cast<overlay_params *>(params_void);
  while (loggingOn){
    uint64_t now = os_time_get();
    elapsedLog = now - log_start;

    currentLogData.fps = fps;
    currentLogData.previous = elapsedLog;
    logArray.push_back(currentLogData);

    if (params->log_duration && (elapsedLog) >= params->log_duration * 1000000)
      loggingOn = false;
    else
      this_thread::sleep_for(chrono::milliseconds(params->log_interval));
  }

  writeFile(params->output_file + get_current_time(), params);
}
