#include "otel_exporter.h"
#include "overlay_params.h"
#include "logging.h"
#include "config.h"
#include "gpu.h"
#include "memory.h"
#include "cpu.h"
#include "overlay.h"

#include <spdlog/spdlog.h>

#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <arpa/inet.h>
  #include <unistd.h>
#endif

// Globals from elsewhere
extern double fps;            // logging.cpp
extern float frametime;       // logging.cpp
extern logData currentLogData;// logging.cpp

static uint64_t monotonic_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

OTelExporter& OTelExporter::instance() {
    static OTelExporter inst;
    return inst;
}

OTelExporter::~OTelExporter() {
    stop();
}

void OTelExporter::stop() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) {
        m_should_run = false;
        return; // Already stopped
    }
    m_should_run = false;
    if (m_collect_thread.joinable()) m_collect_thread.join();
    if (m_server_thread.joinable()) m_server_thread.join();
}

void OTelExporter::reconfigure(const overlay_params* params) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_params_ref = params;
    if (!params->enabled[OVERLAY_PARAM_ENABLED_otel]) {
        stop();
        return;
    }
    m_listen_cached = params->otel_listen;
    m_interval_cached = params->otel_interval > 0 ? params->otel_interval : 1000;
    m_startup_delay_cached = params->otel_startup_delay;
    m_rebuild = true; // Rebuild metrics string ASAP
}

bool OTelExporter::parse_listen(const std::string& listen, std::string& host, std::string& port) {
    auto pos = listen.find(':');
    if (pos == std::string::npos) return false;
    host = listen.substr(0, pos);
    port = listen.substr(pos + 1);
    if (host.empty() || port.empty()) return false;
    return true;
}

void OTelExporter::maybe_start(const overlay_params* params, uint32_t /*vendorID*/) {
    if (!params) return;
    if (!params->enabled[OVERLAY_PARAM_ENABLED_otel]) {
        // If previously running, stop.
        if (m_running) stop();
        return;
    }

    if (!m_should_run) {
        // First time we see it enabled.
        m_should_run = true;
        m_first_enable_call = std::chrono::steady_clock::now();
        m_pid = getpid();
        m_exec_name = get_program_name();
    }

    // Respect startup delay before launching threads
    if (!m_running.load()) {
        auto elapsed = std::chrono::steady_clock::now() - m_first_enable_call;
        if (elapsed < std::chrono::seconds(params->otel_startup_delay)) {
            return; // Wait
        }
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_params_ref = params;
            m_listen_cached = params->otel_listen;
            m_interval_cached = params->otel_interval > 0 ? params->otel_interval : 1000;
            m_startup_delay_cached = params->otel_startup_delay;
        }
        start_locked();
    } else {
        // Already running; detect config changes that require rebuild or restart
        if (m_listen_cached != params->otel_listen || m_interval_cached != params->otel_interval) {
            // For simplicity: full restart if listen address changed; interval change only updated atomically
            bool need_restart = (m_listen_cached != params->otel_listen);
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_params_ref = params;
                m_interval_cached = params->otel_interval > 0 ? params->otel_interval : 1000;
                if (need_restart) {
                    m_listen_cached = params->otel_listen;
                }
                m_rebuild = true;
            }
            if (need_restart) {
                stop();
                start_locked();
            }
        }
    }
}

void OTelExporter::start_locked() {
    if (m_running.load()) return;
    m_running = true;
    try {
        start_threads_unlocked();
        SPDLOG_INFO("OTel metrics exporter started on {} (interval={}ms, delay={}s)", m_listen_cached, m_interval_cached, m_startup_delay_cached);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to start OTEL exporter: {}", e.what());
        m_running = false;
        m_should_run = false;
    }
}

void OTelExporter::start_threads_unlocked() {
    m_collect_thread = std::thread(&OTelExporter::collection_loop, this);
    pthread_setname_np(m_collect_thread.native_handle(), "mangohud-otelc");
    m_server_thread = std::thread(&OTelExporter::server_loop, this);
    pthread_setname_np(m_server_thread.native_handle(), "mangohud-otels");
}

void OTelExporter::collection_loop() {
    while (m_running.load()) {
        if (!m_should_run.load()) break;
        build_metrics_string();
        std::this_thread::sleep_for(std::chrono::milliseconds(m_interval_cached));
    }
}

void OTelExporter::build_metrics_string() {
    // Snapshot global values; rely on MangoHud update threads to keep globals fresh.
    Sample s;
    s.fps = fps;
    s.frametime = frametime; // ms
    s.cpu_load = currentLogData.cpu_load;
    s.gpu_load = currentLogData.gpu_load;
    s.cpu_temp = currentLogData.cpu_temp;
    s.gpu_temp = currentLogData.gpu_temp;
    s.cpu_power = currentLogData.cpu_power;
    s.gpu_power = currentLogData.gpu_power;
    s.ram_used = currentLogData.ram_used;     // MiB/GiB? In log it is MiB? Keep as MiB -> rename MB.
    s.vram_used = currentLogData.gpu_vram_used; // MB
    s.timestamp_ns = monotonic_ns();

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_latest = s;
        std::ostringstream out;
        // Common labels
        out << "# HELP mangohud_fps Current frames per second\n";
        out << "# TYPE mangohud_fps gauge\n";
        out << "mangohud_fps{pid=\"" << m_pid << "\",exec=\"" << m_exec_name << "\"} " << s.fps << "\n";

        out << "# HELP mangohud_frametime_ms Frame time in milliseconds (most recent frame)\n";
        out << "# TYPE mangohud_frametime_ms gauge\n";
        out << "mangohud_frametime_ms{pid=\"" << m_pid << "\",exec=\"" << m_exec_name << "\"} " << s.frametime << "\n";

        out << "# HELP mangohud_cpu_load_percent Average CPU load percent\n";
        out << "# TYPE mangohud_cpu_load_percent gauge\n";
        out << "mangohud_cpu_load_percent{pid=\"" << m_pid << "\",exec=\"" << m_exec_name << "\"} " << s.cpu_load << "\n";

        out << "# HELP mangohud_gpu_load_percent Average GPU load percent\n";
        out << "# TYPE mangohud_gpu_load_percent gauge\n";
        out << "mangohud_gpu_load_percent{pid=\"" << m_pid << "\",exec=\"" << m_exec_name << "\"} " << s.gpu_load << "\n";

        out << "# HELP mangohud_cpu_temp_celsius CPU temperature in Celsius\n";
        out << "# TYPE mangohud_cpu_temp_celsius gauge\n";
        out << "mangohud_cpu_temp_celsius{pid=\"" << m_pid << "\",exec=\"" << m_exec_name << "\"} " << s.cpu_temp << "\n";

        out << "# HELP mangohud_gpu_temp_celsius GPU temperature in Celsius\n";
        out << "# TYPE mangohud_gpu_temp_celsius gauge\n";
        out << "mangohud_gpu_temp_celsius{pid=\"" << m_pid << "\",exec=\"" << m_exec_name << "\"} " << s.gpu_temp << "\n";

        out << "# HELP mangohud_cpu_power_watts CPU package power draw (W)\n";
        out << "# TYPE mangohud_cpu_power_watts gauge\n";
        out << "mangohud_cpu_power_watts{pid=\"" << m_pid << "\",exec=\"" << m_exec_name << "\"} " << s.cpu_power << "\n";

        out << "# HELP mangohud_gpu_power_watts GPU power draw (W)\n";
        out << "# TYPE mangohud_gpu_power_watts gauge\n";
        out << "mangohud_gpu_power_watts{pid=\"" << m_pid << "\",exec=\"" << m_exec_name << "\"} " << s.gpu_power << "\n";

        out << "# HELP mangohud_ram_used_mb System RAM used (MB)\n";
        out << "# TYPE mangohud_ram_used_mb gauge\n";
        out << "mangohud_ram_used_mb{pid=\"" << m_pid << "\",exec=\"" << m_exec_name << "\"} " << s.ram_used << "\n";

        out << "# HELP mangohud_vram_used_mb GPU VRAM used (MB)\n";
        out << "# TYPE mangohud_vram_used_mb gauge\n";
        out << "mangohud_vram_used_mb{pid=\"" << m_pid << "\",exec=\"" << m_exec_name << "\"} " << s.vram_used << "\n";

        m_metrics_text = out.str();
    }
}

void OTelExporter::server_loop() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

    std::string host, port;
    if (!parse_listen(m_listen_cached, host, port)) {
        SPDLOG_ERROR("OTEL exporter: invalid listen address '{}'", m_listen_cached);
        return;
    }

    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *res = nullptr;
    int err = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (err != 0) {
        SPDLOG_ERROR("OTEL exporter: getaddrinfo failed: {}", gai_strerror(err));
        return;
    }

    int listen_fd = -1;
    for (auto p = res; p; p = p->ai_next) {
        listen_fd = (int)socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listen_fd < 0) continue;
        int yes = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));
        if (bind(listen_fd, p->ai_addr, p->ai_addrlen) == 0) {
            if (listen(listen_fd, 8) == 0) break;
        }
#ifdef _WIN32
        closesocket(listen_fd);
#else
        close(listen_fd);
#endif
        listen_fd = -1;
    }
    freeaddrinfo(res);

    if (listen_fd < 0) {
        SPDLOG_ERROR("OTEL exporter: failed to bind {}", m_listen_cached);
        return;
    }

    m_server_ready = true;

    while (m_running.load()) {
        struct sockaddr_storage their_addr;
        socklen_t addr_size = sizeof(their_addr);
        int fd = (int)accept(listen_fd, (struct sockaddr*)&their_addr, &addr_size);
        if (fd < 0) {
            if (!m_running.load()) break;
            continue;
        }
        std::string payload;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            payload = m_metrics_text;
        }
        std::ostringstream http;
        http << "HTTP/1.1 200 OK\r\n";
        http << "Content-Type: text/plain; version=0.0.4\r\n";
        http << "Content-Length: " << payload.size() << "\r\n";
        http << "Connection: close\r\n\r\n";
        http << payload;
        auto txt = http.str();
#ifdef _WIN32
        send(fd, txt.c_str(), (int)txt.size(), 0);
        closesocket(fd);
#else
        ::send(fd, txt.c_str(), txt.size(), 0);
        close(fd);
#endif
    }

#ifdef _WIN32
    if (listen_fd >= 0) closesocket(listen_fd);
    WSACleanup();
#else
    if (listen_fd >= 0) close(listen_fd);
#endif
}

