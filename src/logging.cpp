#include "logging.h"
#include "overlay.h"
#include <sstream>
#include <iomanip>

string os, cpu, gpu, ram, kernel, driver;
bool sysInfoFetched = false;
int gpuLoadLog = 0, cpuLoadLog = 0;
uint64_t elapsedLog;
std::vector<std::string> logFiles;
double fps;
std::vector<logData> logArray;
ofstream out;
bool loggingOn;
uint64_t log_start, log_end;
logData currentLogData = {};
bool logUpdate = false;

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

void writeFile(string filename){
  logFiles.push_back(filename);
  out.open(filename, ios::out | ios::app);
  out << "os," << "cpu," << "gpu," << "ram," << "kernel," << "driver" << endl;
  out << os << "," << cpu << "," << gpu << "," << ram << "," << kernel << "," << driver << endl;
  out << "fps," << "cpu_load," << "gpu_load," << "cpu_temp," << "gpu_temp," << "gpu_core_clock," << "gpu_mem_clock," << "gpu_vram_used," << "ram_used," << "elapsed" << endl;

  for (size_t i = 0; i < logArray.size(); i++){
    out << logArray[i].fps << ",";
    out << logArray[i].cpu_load << ",";
    out << logArray[i].gpu_load << ",";
    out << logArray[i].cpu_temp << ",";
    out << logArray[i].gpu_temp << ",";
    out << logArray[i].gpu_core_clock << ",";
    out << logArray[i].gpu_mem_clock << ",";
    out << logArray[i].gpu_vram_used << ",";
    out << logArray[i].ram_used << ",";
    out << logArray[i].previous << "\n";
  }

  out.close();
  logArray.clear();
}

string get_log_suffix(){
  time_t now_log = time(0);
  tm *log_time = localtime(&now_log);
  std::ostringstream buffer;
  buffer << std::put_time(log_time, "%Y-%m-%d_%H-%M-%S") << ".csv";
  string log_name = buffer.str();
  return log_name;
}

void logging(void *params_void){
  overlay_params *params = reinterpret_cast<overlay_params *>(params_void);
  while (loggingOn){
      uint64_t now = os_time_get();
      elapsedLog = now - log_start;

      currentLogData.fps = fps;
      currentLogData.previous = elapsedLog;
      if (logUpdate)
        logArray.push_back(currentLogData);

      if (params->log_duration && (elapsedLog) >= params->log_duration * 1000000)
        loggingOn = false;
      else
        this_thread::sleep_for(chrono::milliseconds(params->log_interval));

  }

  writeFile(params->output_file + get_log_suffix());
}
