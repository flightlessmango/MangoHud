#include <sstream>
#include <iomanip>
#include <array>
#include <algorithm>
#include <spdlog/spdlog.h>
#include "logging.h"
#include "overlay.h"
#include "config.h"
#include "file_utils.h"
#include "string_utils.h"
#include "version.h"
#include "fps_metrics.h"

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
    auto deleter = [](FILE* ptr){ pclose(ptr); };
    std::unique_ptr<FILE, decltype(deleter)> pipe(popen(command.c_str(), "r"), deleter);
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
  // if the log is stopped/started too fast we might end up with an empty vector.
  // in that case, just bail.
  if (logArray.size() == 0){
    logger->stop_logging();
    return;
  }

  filename = filename.substr(0, filename.size() - 4);
  filename += "_summary.csv";
  SPDLOG_INFO("{}", filename);
  SPDLOG_DEBUG("Writing summary log file [{}]", filename);
  std::ofstream out(filename, ios::out | ios::app);
  if (out){
    out << "0.1% Min FPS," << "1% Min FPS," << "97% Percentile FPS," 
        << "Average FPS," << "GPU Load," << "CPU Load," << "Average Frame Time,"
        << "Average GPU Temp," << "Average CPU Temp," << "Average VRAM Used,"
        << "Average RAM Used," << "Average Swap Used," << "Peak GPU Load,"
        << "Peak CPU Load," << "Peak GPU Temp," << "Peak CPU Temp,"
        << "Peak VRAM Used," << "Peak RAM Used," << "Peak Swap Used" << "\n";
    std::vector<logData> sorted = logArray;
    std::sort(sorted.begin(), sorted.end(), compareByFps);
    float total = 0.0f;
    float total_gpu = 0.0f;
    float total_cpu = 0.0f;
    int total_gpu_temp = 0.0f;
    int total_cpu_temp = 0.0f;
    float total_vram = 0.0f;
    float total_ram = 0.0f;
    float total_swap = 0.0f;
    int peak_gpu = 0.0f;
    float peak_cpu = 0.0f;
    int peak_gpu_temp = 0.0f;
    int peak_cpu_temp = 0.0f;
    float peak_vram = 0.0f;
    float peak_ram = 0.0f;
    float peak_swap = 0.0f;
    float result;
    std::vector<float> fps_values;
    for (auto& data : sorted)
      fps_values.push_back(data.frametime);

    std::unique_ptr<fpsMetrics> fpsmetrics;
    std::vector<std::string> metrics {"0.001", "0.01", "0.97"};
    fpsmetrics = std::make_unique<fpsMetrics>(metrics, fps_values);
    for (auto& metric : fpsmetrics->metrics)
      out << metric.value << ",";

    fpsmetrics.reset();

    total = 0;
    for (auto input : sorted){
      total = total + input.frametime;
      total_gpu = total_gpu + input.gpu_load;
      total_cpu = total_cpu + input.cpu_load;
      total_gpu_temp = total_gpu_temp + input.gpu_temp;
      total_cpu_temp = total_cpu_temp + input.cpu_temp;
      total_vram = total_vram + input.gpu_vram_used;
      total_ram = total_ram + input.ram_used;
      total_swap = total_swap + input.swap_used;
      peak_gpu = std::max(peak_gpu, input.gpu_load);
      peak_cpu = std::max(peak_cpu, input.cpu_load);
      peak_gpu_temp = std::max(peak_gpu_temp, input.gpu_temp);
      peak_cpu_temp = std::max(peak_cpu_temp, input.cpu_temp);
      peak_vram = std::max(peak_vram, input.gpu_vram_used);
      peak_ram = std::max(peak_ram, input.ram_used);
      peak_swap = std::max(peak_swap, input.swap_used);
    }
    // Average FPS
    result = 1000 / (total / sorted.size());
    out << fixed << setprecision(1) << result << ",";
    // GPU Load (Average)
    result = total_gpu / sorted.size();
    out << result << ",";
    // CPU Load (Average)
    result = total_cpu / sorted.size();
    out << result << ",";
    // Average Frame Time
    result = total / sorted.size();
    out << result << ",";
    // Average GPU Temp
    result = total_gpu_temp / sorted.size();
    out << result << ",";
    // Average CPU Temp
    result = total_cpu_temp / sorted.size();
    out << result << ",";
    // Average VRAM Used
    result = total_vram / sorted.size();
    out << result << ",";
    // Average RAM Used
    result = total_ram / sorted.size();
    out << result << ",";
    // Average Swap Used
    result = total_swap / sorted.size();
    out << result << ",";
    // Peak GPU Load
    out << peak_gpu << ",";
    // Peak CPU Load
    out << peak_cpu << ",";
    // Peak GPU Temp
    out << peak_gpu_temp << ",";
    // Peak CPU Temp
    out << peak_cpu_temp << ",";
    // Peak VRAM Used
    out << peak_vram << ",";
    // Peak RAM Used
    out << peak_ram << ",";
    // Peak Swap Used
    out << peak_swap;
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

    out << "fps," << "frametime," << "cpu_load," << "cpu_power," << "gpu_load,"
        << "cpu_temp," << "gpu_temp," << "gpu_core_clock," << "gpu_mem_clock,"
        << "gpu_vram_used," << "gpu_power," << "ram_used," << "swap_used,"
        << "process_rss," << "cpu_mhz," << "elapsed" << endl;

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
    output_file << logArray.back().cpu_power << ",";
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
    output_file << logArray.back().cpu_mhz << ",";
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
    pthread_setname_np(log_thread.native_handle(), "logging");
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
  vector<float> fps_values {};
  for (auto& point : m_log_array)
    fps_values.push_back(point.frametime);

  benchmark.percentile_data.clear();

  std::vector<std::string> metrics {"0.97", "avg", "0.01", "0.001"};
  std::unique_ptr<fpsMetrics> fpsmetrics;
  if (!HUDElements.params->fps_metrics.empty())
    metrics = HUDElements.params->fps_metrics;
    
  fpsmetrics = std::make_unique<fpsMetrics>(metrics, fps_values);
  for (auto& metric : fpsmetrics->metrics)
    benchmark.percentile_data.push_back({metric.display_name, metric.value});

  fpsmetrics.reset();
}
