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
#ifndef _WIN32
    if (getenv("LD_PRELOAD"))
        unsetenv("LD_PRELOAD");
#endif
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

bool compareByFps(const logData &a, const logData &b)
{
    return a.fps < b.fps;
}

void writeSummary(string filename){
  auto& logArray = logger->get_log_data();
  filename = filename.substr(0, filename.size() - 4);
  filename += "_summary.csv";
  printf("%s\n", filename.c_str());
  SPDLOG_DEBUG("Writing summary log file [{}]", filename, logArray.size());
  std::ofstream out(filename, ios::out | ios::app);
  if (out){
    out << "0.1% Min FPS," << "1% Min FPS," << "97% Percentile FPS," << "Average FPS," << "GPU Load," << "CPU Load" << "\n";
    std::vector<logData> sorted = logArray;
    std::sort(sorted.begin(), sorted.end(), compareByFps);
    float total = 0.0f;
    float total_cpu = 0.0f;
    float total_gpu = 0.0f;
    float result;
    float percents[2] = {0.001, 0.01};
    for (auto percent : percents){
      total = 0;
      size_t idx = ceil(sorted.size() * percent);
      for (size_t i = 0; i < idx; i++){
        total = total + sorted[i].fps;
      }
      result = total / idx;
      out << fixed << setprecision(1) << result << ",";
    }
    // 97th percentile
    result = sorted.empty() ? 0.0f : sorted[floor(0.97 * (sorted.size() - 1))].fps;
    out << fixed << setprecision(1) << result << ",";
    // avg
    total = 0;
    for (auto input : sorted){
      total = total + input.fps;
      total_cpu = total_cpu + input.cpu_load;
      total_gpu = total_gpu + input.gpu_load;
    }
    result = total / sorted.size();
    out << fixed << setprecision(1) << result << ",";
    // GPU
    result = total_gpu / sorted.size();
    out << result << ",";
    // CPU
    result = total_cpu / sorted.size();
    out << result;
  } else {
    printf("MANGOHUD: Failed to write log file\n");
  }
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

Logger::Logger(overlay_params* in_params)
  : m_params(in_params),
    m_logging_on(false),
    m_values_valid(false)
{
  m_log_end = Clock::now() - 15s;
  SPDLOG_DEBUG("Logger constructed!");
}

void Logger::start_logging() {
  if(m_logging_on) return;
  m_values_valid = false;
  m_logging_on = true;
  m_log_start = Clock::now();
#ifdef MANGOAPP
  HUDElements.params->log_interval = 0;
#endif
  if((!m_params->output_folder.empty()) && (m_params->log_interval != 0)){
    std::thread(&Logger::logging, this).detach();
  }
}

void Logger::stop_logging() {
  if(!m_logging_on) return;
  m_logging_on = false;
  m_log_end = Clock::now();

  calculate_benchmark_data();

  if(!m_params->output_folder.empty()) {
    std::string program = get_wine_exe_name();
    if (program.empty())
        program = get_program_name();
    m_log_files.emplace_back(m_params->output_folder + "/" + program + "_" + get_log_suffix());
    std::thread writefile (writeFile, m_log_files.back());
    std::thread writesummary (writeSummary, m_log_files.back());
    writefile.join();
    writesummary.join();
  } else {
#ifdef MANGOAPP
    string path = std::getenv("HOME");
    std::string logName = path + "/mangoapp_" + get_log_suffix();
    writeSummary(logName);
    writeFile(logName);
#endif
  }
  clear_log_data();
}

void Logger::logging(){
  wait_until_data_valid();
  while (is_active()){
      try_log();
      this_thread::sleep_for(chrono::milliseconds(m_params->log_interval));
  }
}

void Logger::try_log() {
  if(!is_active()) return;
  if(!m_values_valid) return;
  auto now = Clock::now();
  auto elapsedLog = now - m_log_start;

  currentLogData.previous = elapsedLog;
  currentLogData.fps = 1000.f / (frametime / 1000.f);
  currentLogData.frametime = frametime;
  m_log_array.push_back(currentLogData);

  if(m_params->log_duration && (elapsedLog >= std::chrono::seconds(m_params->log_duration))){
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

void Logger::calculate_benchmark_data(){
  vector<float> sorted = benchmark.fps_data;
  std::sort(sorted.begin(), sorted.end());
  benchmark.percentile_data.clear();

  benchmark.total = 0.f;
  for (auto fps_ : sorted){
    benchmark.total = benchmark.total + fps_;
  }

  size_t max_label_size = 0;

  float result;
  for (std::string percentile : m_params->benchmark_percentiles) {
      // special case handling for a mean-based average
      if (percentile == "AVG") {
        result = benchmark.total / sorted.size();
      } else {
        // the percentiles are already validated when they're parsed from the config.
        float fraction = parse_float(percentile) / 100;

        result = sorted.empty() ? 0.0f : sorted[(fraction * sorted.size()) - 1];
        percentile += "%";
      }

      if (percentile.length() > max_label_size)
        max_label_size = percentile.length();

      benchmark.percentile_data.push_back({percentile, result});
  }
  string label;
  float mins[2] = {0.01f, 0.001f}, total;
  for (auto percent : mins){
    total = 0;
    size_t idx = ceil(sorted.size() * percent);
    for (size_t i = 0; i < idx; i++){
      total = total + sorted[i];
    }
    result = total / idx;

    if (percent == 0.001f)
      label = "0.1%";
    if (percent == 0.01f)
      label = "1%";

    if (label.length() > max_label_size)
      max_label_size = label.length();

    benchmark.percentile_data.push_back({label, result});
  }

   for (auto& entry : benchmark.percentile_data) {
      entry.first.append(max_label_size - entry.first.length(), ' ');
   }
}
