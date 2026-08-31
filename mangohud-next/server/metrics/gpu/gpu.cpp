#include <algorithm>
#include <cstdlib>

#include "gpu.hpp"

void GPU::check_pids_existence() {
    std::set<pid_t> pids_to_delete;

    for (const auto& p : process_metrics) {
        pid_t pid = p.first;

        if (!fs::exists("/proc/" + std::to_string(pid)))
            pids_to_delete.insert(pid);
    }

    for (const auto& p : pids_to_delete)
        process_metrics.erase(p);
}

void GPU::poll() {
    while (!stop_thread) {
        SPDLOG_TRACE("poll()");

        SystemMetricsBuffer sys_metrics_buffer = {};
        std::map<pid_t, ProcessMetricsBuffer> proc_metrics_buffers;
        size_t samples = 0;

        for (auto& sys_metrics : sys_metrics_buffer) {
            if (stop_thread)
                break;

            auto current_time = std::chrono::steady_clock::now();
            delta_time_ns = current_time - previous_time;
            previous_time = current_time;

            pre_poll_overrides();

            sys_metrics = {
                .load                   = get_load(),

                .vram_used              = get_vram_used(),
                .gtt_used               = get_gtt_used(),
                .memory_total           = get_memory_total(),
                .memory_clock           = get_memory_clock(),
                .memory_temp            = get_memory_temp(),

                .temperature            = get_temperature(),
                .junction_temperature   = get_junction_temperature(),

                .core_clock             = get_core_clock(),
                .voltage                = get_voltage(),

                .power_usage            = get_power_usage(),
                .power_limit            = get_power_limit(),

                .is_apu                 = get_is_apu(),
                .apu_cpu_power          = get_apu_cpu_power(),
                .apu_cpu_temp           = get_apu_cpu_temp(),

                .is_power_throttled     = get_is_power_throttled(),
                .is_current_throttled   = get_is_current_throttled(),
                .is_temp_throttled      = get_is_temp_throttled(),
                .is_other_throttled     = get_is_other_throttled(),

                .fan_speed              = get_fan_speed(),
                .fan_rpm                = get_fan_rpm()
            };

            std::vector<pid_t> pids;
            {
                std::unique_lock proc_lock(process_metrics_mutex);
                check_pids_existence();
                pids.reserve(process_metrics.size());
                for (const auto& p : process_metrics)
                    pids.push_back(p.first);
            }

            for (pid_t pid : pids) {
                proc_metrics_buffers[pid][samples] = {
                    .load = get_process_load(pid),
                    .vram_used = get_process_vram_used(pid),
                    .gtt_used = get_process_gtt_used(pid)
                };
            }

            samples++;

            if (samples >= gpu_metrics_sample_count)
                continue;

            auto elapsed = std::chrono::steady_clock::now() - current_time;
            if (elapsed < gpu_metrics_polling_period)
                std::this_thread::sleep_for(gpu_metrics_polling_period - elapsed);
        }

        if (!samples || stop_thread)
            return;

        {
            std::unique_lock sys_lock(system_metrics_mutex);
            system_metrics = average_system_metrics(sys_metrics_buffer, samples);
        }

        {
            std::unique_lock proc_lock(process_metrics_mutex);
            for (const auto& [pid, metrics_buffer] : proc_metrics_buffers)
                process_metrics[pid] = average_process_metrics(metrics_buffer, samples);
        }
    }
}

static int render_minor_from_node(const std::string& drm_node) {
    if (!drm_node.starts_with("renderD"))
        return -1;

    char* end = nullptr;
    long renderer = std::strtol(drm_node.c_str() + 7, &end, 10);
    if (end && *end == '\0' && renderer >= 0)
        return static_cast<int>(renderer);

    return -1;
}

GPU::GPU(
    const std::string& drm_node, const std::string& pci_dev,
    uint16_t vendor_id, uint16_t device_id, const std::string& thread_name
) : drm_node(drm_node), pci_dev(pci_dev), vendor_id(vendor_id),
    device_id(device_id), render_minor(render_minor_from_node(drm_node)),
    worker_thread_name(thread_name) {}

int GPU::renderer() const {
    return render_minor;
}

GPU::~GPU() {
    stop_polling();
}

void GPU::add_pid(pid_t pid) {
    std::unique_lock lock(process_metrics_mutex);
    process_metrics.try_emplace(pid);
}

gpu_metrics_system_t GPU::get_system_metrics() {
    SPDLOG_TRACE("GPU get_system_metrics()");
    std::unique_lock lock(system_metrics_mutex);
    return system_metrics;
}

std::map<pid_t, gpu_metrics_process_t> GPU::get_process_metrics() {
    SPDLOG_TRACE("GPU get_process_metrics");
    std::unique_lock lock(process_metrics_mutex);
    return process_metrics;
}

gpu_metrics_process_t GPU::get_process_metrics(const size_t pid) {
    SPDLOG_TRACE("GPU get_process_metrics");
    std::unique_lock lock(process_metrics_mutex);
    return process_metrics[pid];
}

void GPU::print_metrics() {
    std::unique_lock sys_lock(system_metrics_mutex);
    std::unique_lock proc_lock(process_metrics_mutex);

    SPDLOG_TRACE("==========================");
    SPDLOG_TRACE("load                 = {}\n", system_metrics.load);

    // SPDLOG_TRACE("vram_used            = {}", vram_used);
    // SPDLOG_TRACE("gtt_used             = {}", gtt_used);
    // SPDLOG_TRACE("memory_total         = {}", memory_total);
    // SPDLOG_TRACE("memory_clock         = {}", memory_clock);
    // SPDLOG_TRACE("memory_temp          = {}\n", memory_temp);

    // SPDLOG_TRACE("temperature          = {}", temperature);
    // SPDLOG_TRACE("junction_temperature = {}\n", junction_temperature);

    // SPDLOG_TRACE("core_clock           = {}", core_clock);
    SPDLOG_TRACE("voltage              = {}\n", system_metrics.voltage);

    SPDLOG_TRACE("power_usage          = {}", system_metrics.power_usage);
    SPDLOG_TRACE("power_limit          = {}\n", system_metrics.power_limit);

    // SPDLOG_TRACE("apu_cpu_power        = {}", apu_cpu_power);
    // SPDLOG_TRACE("apu_cpu_temp         = {}\n", apu_cpu_temp);

    // SPDLOG_TRACE("is_power_throttled   = {}", is_power_throttled);
    // SPDLOG_TRACE("is_current_throttled = {}", is_current_throttled);
    // SPDLOG_TRACE("is_temp_throttled    = {}", is_temp_throttled);
    // SPDLOG_TRACE("is_other_throttled   = {}\n", is_other_throttled);

    // SPDLOG_TRACE("fan_speed            = {}", fan_speed);
    // SPDLOG_TRACE("fan_rpm              = {}\n", fan_rpm);

    SPDLOG_TRACE("Process stats:");

    for (const auto& p : process_metrics) {
        pid_t pid = p.first;
        gpu_metrics_process_t m = p.second;
        SPDLOG_TRACE("    {}:", pid);
        SPDLOG_TRACE("        load      = {}", m.load);
        SPDLOG_TRACE("        vram_used = {}", m.vram_used);
        SPDLOG_TRACE("        gtt_used  = {}\n", m.gtt_used);
    }

    SPDLOG_TRACE("==========================\n");
}

void GPU::start_thread_worker() {
    worker_thread = std::thread(&GPU::poll, this);

    if (worker_thread_name.length() > 15)
        SPDLOG_DEBUG(
            "thread name \"{}\" is longer than allowed linux maximum of 15 characters!"
        );

    pthread_setname_np(worker_thread.native_handle(), worker_thread_name.c_str());
}

void GPU::request_polling() {
    std::lock_guard lock(worker_mutex);
    last_poll_request = std::chrono::steady_clock::now();
    if (worker_thread.joinable())
        return;

    stop_thread = false;
    start_thread_worker();
}

void GPU::stop_polling() {
    std::lock_guard lock(worker_mutex);
    if (!worker_thread.joinable())
        return;

    stop_thread = true;
    worker_thread.join();
}

void GPU::stop_polling_if_idle(std::chrono::steady_clock::time_point now,
                               std::chrono::nanoseconds idle_timeout) {
    std::lock_guard lock(worker_mutex);
    if (!worker_thread.joinable())
        return;

    if (now - last_poll_request < idle_timeout)
        return;

    stop_thread = true;
    worker_thread.join();
}

bool GPU::polling_active() const {
    std::lock_guard lock(worker_mutex);
    return worker_thread.joinable();
}
