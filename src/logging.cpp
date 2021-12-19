#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>
#include "logging.h"
#include "overlay.h"
#include "config.h"
#include "file_utils.h"
#include "string_utils.h"

string os, cpu, gpu, ram, kernel, driver, cpusched;
bool sysInfoFetched = false;
double fps;
uint64_t frametime;
logData currentLogData = {};

std::unique_ptr<Logger> logger;

string exec(string command) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
      return "popen failed!";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
      result += buffer.data();
    }
    return result;
}

void upload_file(std::string logFile){
  std::string command = "curl --include --request POST https://flightlessmango.com/logs -F 'log[game_id]=26506' -F 'log[user_id]=176' -F 'attachment=true' -A 'mangohud' ";
  command += " -F 'log[uploads][]=@" + logFile + "'";

  command += " | grep Location | cut -c11-";
  std::string url = exec(command);
  exec("xdg-open " + url);
}

void upload_files(const std::vector<std::string>& logFiles){
  std::string command = "curl --include --request POST https://flightlessmango.com/logs -F 'log[game_id]=26506' -F 'log[user_id]=176' -F 'attachment=true' -A 'mangohud' ";
  for (auto& file : logFiles)
    command += " -F 'log[uploads][]=@" + file + "'";

  command += " | grep Location | cut -c11-";
  std::string url = exec(command);
  exec("xdg-open " + url);
}

void writeFile(string filename){
  auto& logArray = logger->get_log_data();
  SPDLOG_DEBUG("Writing log file [{}], {} entries", filename, logArray.size());
  std::ofstream out(filename, ios::out | ios::app);
  if (out){
  out << "os," << "cpu," << "gpu," << "ram," << "kernel," << "driver," << "cpuscheduler" << endl;
  out << os << "," << cpu << "," << gpu << "," << ram << "," << kernel << "," << driver << "," << cpusched << endl;
  out << "fps," << "frametime," << "cpu_load," << "gpu_load," << "cpu_temp," << "gpu_temp," << "gpu_core_clock," << "gpu_mem_clock," << "gpu_vram_used," << "gpu_power," << "ram_used," << "elapsed" << endl;

  for (size_t i = 0; i < logArray.size(); i++){
    out << logArray[i].fps << ",";
    out << logArray[i].frametime << ",";
    out << logArray[i].cpu_load << ",";
    out << logArray[i].gpu_load << ",";
    out << logArray[i].cpu_temp << ",";
    out << logArray[i].gpu_temp << ",";
    out << logArray[i].gpu_core_clock << ",";
    out << logArray[i].gpu_mem_clock << ",";
    out << logArray[i].gpu_vram_used << ",";
    out << logArray[i].gpu_power << ",";
    out << logArray[i].ram_used << ",";
    out << std::chrono::duration_cast<std::chrono::nanoseconds>(logArray[i].previous).count() << "\n";
  }
  logger->clear_log_data();
  } else {
    printf("MANGOHUD: Failed to write log file\n");
  }
}

string get_log_suffix(){
  time_t now_log = time(0);
  tm *log_time = localtime(&now_log);
  std::ostringstream buffer;
  buffer << std::put_time(log_time, "%Y-%m-%d_%H-%M-%S") << ".csv";
  string log_name = buffer.str();
  return log_name;
}

void logging(){
  logger->wait_until_data_valid();
  while (logger->is_active()){
      logger->try_log();
      this_thread::sleep_for(chrono::milliseconds(_params->log_interval));
  }
}

Logger::Logger(overlay_params* in_params)
  : m_logging_on(false),
    m_values_valid(false),
    m_params(in_params)
{
  SPDLOG_DEBUG("Logger constructed!");
}

void Logger::start_logging() {
  if(m_logging_on) return;
  m_values_valid = false;
  m_logging_on = true;
  m_log_start = Clock::now();
  if((!_params->output_folder.empty()) && (_params->log_interval != 0)){
    std::thread(logging).detach();
  }
}

void Logger::stop_logging() {
  if(!m_logging_on) return;
  m_logging_on = false;
  m_log_end = Clock::now();

  calculate_benchmark_data(m_params);

  if(!_params->output_folder.empty()) {
    std::string program = get_wine_exe_name();
    if (program.empty())
        program = get_program_name();
    m_log_files.emplace_back(_params->output_folder + "/" + program + "_" + get_log_suffix());
    std::thread(writeFile, m_log_files.back()).detach();
  }
}

void Logger::try_log() {
  if(!is_active()) return;
  if(!m_values_valid) return;
  auto now = Clock::now();
  auto elapsedLog = now - m_log_start;

  currentLogData.previous = elapsedLog;
  currentLogData.fps = fps;
  currentLogData.frametime = frametime;
  m_log_array.push_back(currentLogData);

  if(_params->log_duration && (elapsedLog >= std::chrono::seconds(_params->log_duration))){
    stop_logging();
  }
}

void Logger::wait_until_data_valid() {
  std::unique_lock<std::mutex> lck(m_values_valid_mtx);
  while(! m_values_valid) m_values_valid_cv.wait(lck);
}

void Logger::notify_data_valid() {
  std::unique_lock<std::mutex> lck(m_values_valid_mtx);
  m_values_valid = true;
  m_values_valid_cv.notify_all();
}

void Logger::upload_last_log() {
  if(m_log_files.empty()) return;
  std::thread(upload_file, m_log_files.back()).detach();
}

void Logger::upload_last_logs() {
  if(m_log_files.empty()) return;
  std::thread(upload_files, m_log_files).detach();
}

void autostart_log(int sleep) {
  os_time_sleep(sleep * 1000000);
  logger->start_logging();
}
