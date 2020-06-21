#include "logging.h"
#include "overlay.h"
#include <sstream>
#include <iomanip>

string os, cpu, gpu, ram, kernel, driver;
bool sysInfoFetched = false;
std::vector<std::string> logFiles;
double fps;
logData currentLogData = {};

std::unique_ptr<Logger> logger;

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
  auto& logArray = logger->get_log_data();
  std::cerr << "writeFile(" << filename << ", vector<logData>(" << logArray.size() << "))\n";
  logFiles.push_back(filename);
  std::ofstream out(filename, ios::out | ios::app);
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
    out << std::chrono::duration_cast<std::chrono::microseconds>(logArray[i].previous).count() << "\n";
  }
  logger->clear_log_data();
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
  logger->wait_until_data_valid();
  while (logger->is_active()){
      logger->try_log();
      this_thread::sleep_for(chrono::milliseconds(params->log_interval));
  }
}

Logger::Logger(overlay_params* in_params)
  : loggingOn(false), 
    values_valid(false), 
    params(in_params)
{
#ifndef NDEBUG
  std::cerr << "Logger constructed!\n";
#endif
}

void Logger::start_logging() {
  if(loggingOn) return;
  values_valid = false;
  loggingOn = true;
  log_start = Clock::now();
  if((not params->output_file.empty()) and (params->log_interval != 0)){
    std::thread(logging, params).detach();
  }
}

void Logger::stop_logging() {
  if(not loggingOn) return;
  loggingOn = false;
  log_end = Clock::now();

  std::thread(calculate_benchmark_data).detach();

  if(not params->output_file.empty()) {
    std::thread(writeFile, params->output_file + get_log_suffix()).detach();
  }
}


void Logger::try_log() {
  if(not is_active()) return;
  if(not values_valid) return;
  auto now = Clock::now();
  auto elapsedLog = now - log_start;

  currentLogData.previous = elapsedLog;
  currentLogData.fps = fps;
  logArray.push_back(currentLogData);

  if(params->log_duration and (elapsedLog >= std::chrono::seconds(params->log_duration))){
    stop_logging();
  }
}

void Logger::wait_until_data_valid() {
  std::unique_lock<std::mutex> lck(values_valid_mtx);
  while(! values_valid) values_valid_cv.wait(lck);
}

void Logger::notify_data_valid() {
  std::unique_lock<std::mutex> lck(values_valid_mtx);
  values_valid = true;
  values_valid_cv.notify_all();
}