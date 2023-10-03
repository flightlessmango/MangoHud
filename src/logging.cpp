#include <sstream>
#include <iomanip>
#include <array>
#include <spdlog/spdlog.h>
#include "logging.h"
#include "overlay.h"
#include "config.h"
#include "file_utils.h"
#include "string_utils.h"
#include "version.h"

using namespace std;

string os, cpu, gpu, ram, kernel, driver, cpusched;
bool sysInfoFetched = false;
double fps;
float frametime;
logData currentLogData = {};
std::unique_ptr<Logger> logger;
ofstream output_file;
std::thread log_thread;

string exec(string command) {
#ifndef _WIN32
    command = "unset LD_PRELOAD; " + command;
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

static void upload_file(std::string logFile){
  std::string command = "curl --include --request POST https://flightlessmango.com/logs -F 'log[game_id]=26506' -F 'log[user_id]=176' -F 'attachment=true' -A 'mangohud' ";
  command += " -F 'log[uploads][]=@" + logFile + "'";

  command += " | grep Location | cut -c11-";
  std::string url = exec(command);
  std::cout << "upload url: " << url;
  exec("xdg-open " + url);
}

static void upload_files(const std::vector<std::string>& logFiles){
  std::string command = "curl --include --request POST https://flightlessmango.com/logs -F 'log[game_id]=26506' -F 'log[user_id]=176' -F 'attachment=true' -A 'mangohud' ";
  for (auto& file : logFiles)
    command += " -F 'log[uploads][]=@" + file + "'";

  command += " | grep Location | cut -c11-";
  std::string url = exec(command);
  std::cout << "upload url: " << url;
  exec("xdg-open " + url);
}

static bool compareByFps(const logData &a, const logData &b)
{
    return a.fps < b.fps;
}

static void writeSummary(string filename){
  auto& logArray = logger->get_log_data();
  filename = filename.substr(0, filename.size() - 4);
  filename += "_summary.csv";
  SPDLOG_INFO("{}", filename);
  SPDLOG_DEBUG("Writing summary log file [{}]", filename);
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
        total = total + sorted[i].frametime;
      }
      result = 1000 / (total / idx);
      out << fixed << setprecision(1) << result << ",";
    }
    // 97th percentile
    result = sorted.empty() ? 0.0f : 1000 / sorted[floor(0.97 * (sorted.size() - 1))].frametime;
    out << fixed << setprecision(1) << result << ",";
    // avg
    total = 0;
    for (auto input : sorted){
      total = total + input.frametime;
      total_cpu = total_cpu + input.cpu_load;
      total_gpu = total_gpu + input.gpu_load;
    }
    result = 1000 / (total / sorted.size());
    out << fixed << setprecision(1) << result << ",";
    // GPU
    result = total_gpu / sorted.size();
    out << result << ",";
    // CPU
    result = total_cpu / sorted.size();
    out << result;
  } else {
    SPDLOG_ERROR("Failed to write log file");
  }
  out.close();
}

static void writeFileHeaders(ofstream& out){
      if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_log_versioning]){
      printf("log versioning");
      out << "v1" << endl;
      out << MANGOHUD_VERSION << endl;
      out << "---------------------SYSTEM INFO---------------------" << endl;
    }

    out << "os," << "cpu," << "gpu," << "ram," << "kernel," << "driver," << "cpuscheduler" << endl;
    out << os << "," << cpu << "," << gpu << "," << ram << "," << kernel << "," << driver << "," << cpusched << endl;

    if (HUDElements.params->enabled[OVERLAY_PARAM_ENABLED_log_versioning])
      out << "--------------------FRAME METRICS--------------------" << endl;

    out << "fps," << "frametime," << "cpu_load," << "gpu_load," << "cpu_temp," << "gpu_temp," << "gpu_core_clock," << "gpu_mem_clock," << "gpu_vram_used," << "gpu_power," << "ram_used," << "swap_used," << "process_rss," << "elapsed" << endl;

}

void Logger::writeToFile(){
  if (!output_file){
    output_file.open(m_log_files.back(), ios::out | ios::app);
    writeFileHeaders(output_file);
  }

  auto& logArray = logger->get_log_data();
  if (output_file && !logArray.empty()){
    output_file << logArray.back().fps << ",";
    output_file << logArray.back().frametime << ",";
    output_file << logArray.back().cpu_load << ",";
    output_file << logArray.back().gpu_load << ",";
    output_file << logArray.back().cpu_temp << ",";
    output_file << logArray.back().gpu_temp << ",";
    output_file << logArray.back().gpu_core_clock << ",";
    output_file << logArray.back().gpu_mem_clock << ",";
    output_file << logArray.back().gpu_vram_used << ",";
    output_file << logArray.back().gpu_power << ",";
    output_file << logArray.back().ram_used << ",";
    output_file << logArray.back().swap_used << ",";
    output_file << logArray.back().process_rss << ",";
    output_file << std::chrono::duration_cast<std::chrono::nanoseconds>(logArray.back().previous).count() << "\n";
    output_file.flush();
  } else {
    printf("MANGOHUD: Failed to write log file\n");
  }
}

static string get_log_suffix(){
  time_t now_log = time(0);
  tm *log_time = localtime(&now_log);
  std::ostringstream buffer;
  buffer << std::put_time(log_time, "%Y-%m-%d_%H-%M-%S") << ".csv";
  string log_name = buffer.str();
  return log_name;
}

Logger::Logger(const overlay_params* in_params)
  : output_folder(in_params->output_folder),
	log_interval(in_params->log_interval),
	log_duration(in_params->log_duration),
    m_logging_on(false),
    m_values_valid(false)
{
  if(output_folder.empty()) output_folder = std::getenv("HOME");
  m_log_end = Clock::now() - 15s;
  SPDLOG_DEBUG("Logger constructed!");
}

void Logger::start_logging() {
  if(m_logging_on) return;
  m_values_valid = false;
  m_logging_on = true;
  m_log_start = Clock::now();

  std::string program = get_wine_exe_name();

  if (program.empty())
      program = get_program_name();

  m_log_files.emplace_back(output_folder + "/" + program + "_" + get_log_suffix());

  if(log_interval != 0){
    std::thread log_thread(&Logger::logging, this);
    log_thread.detach();
  }
}

void Logger::stop_logging() {
  if(!m_logging_on) return;
  m_logging_on = false;
  m_log_end = Clock::now();
  if (log_thread.joinable()) log_thread.join();

  calculate_benchmark_data();
  output_file.close();
  writeSummary(m_log_files.back());
  clear_log_data();
#ifdef __linux__
  control_client_check(HUDElements.params->control, global_control_client, gpu.c_str());
  const char * cmd = "LoggingFinished";
  control_send(global_control_client, cmd, strlen(cmd), 0, 0);
#endif
}

void Logger::logging(){
  wait_until_data_valid();
  while (is_active()){
      try_log();
      this_thread::sleep_for(std::chrono::milliseconds(log_interval));
  }
  clear_log_data();
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
  writeToFile();

  if(log_duration && (elapsedLog >= std::chrono::seconds(log_duration))){
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
  // os_time_sleep() causes freezes with zink + autologging :frog_donut:
  this_thread::sleep_for(chrono::seconds(sleep));
  logger->start_logging();
}

void Logger::calculate_benchmark_data(){
  vector<float> sorted {};
  for (auto& point : m_log_array)
    sorted.push_back(point.frametime);

  std::sort(sorted.begin(), sorted.end(), [](float a, float b) {
    return a > b;
  });
  benchmark.percentile_data.clear();

  benchmark.total = 0.f;
  for (auto frametime_ : sorted){
    benchmark.total = benchmark.total + frametime_;
  }

  size_t max_label_size = 0;

  float result;
  for (std::string percentile : HUDElements.params->benchmark_percentiles) {
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

      benchmark.percentile_data.push_back({percentile, (1000 / result)});
  }
  string label;
  float mins[2] = {0.01f, 0.001f};
  for (auto percent : mins){
    size_t percentile_pos = sorted.size() * percent;
    percentile_pos = std::min(percentile_pos, sorted.size() - 1);
    float result = 1000 / sorted[percentile_pos];

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
