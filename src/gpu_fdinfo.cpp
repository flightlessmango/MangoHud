#include "gpu_fdinfo.h"
namespace fs = ghc::filesystem;

void GPU_fdinfo::find_fd()
{
    auto path = fs::path("/proc/self/fdinfo");

    if (!fs::exists(path)) {
        SPDLOG_DEBUG("{} does not exist", path.string());
        return;
    }

    // Here we store client-ids, if ids match, we dont open this file,
    // because it will have same readings and it becomes a duplicate
    std::set<std::string> client_ids;
    int total = 0;

    for (const auto& entry : fs::directory_iterator(path)) {
        auto fd_path = entry.path().string();
        auto file = std::ifstream(fd_path);

        if (!file.is_open())
            continue;

        std::string driver, pdev, client_id;

        for (std::string line; std::getline(file, line);) {
            auto key = line.substr(0, line.find(":"));
            auto val = line.substr(key.length() + 2);

            if (key == "drm-driver")
                driver = val;
            else if (key == "drm-pdev")
                pdev = val;
            else if (key == "drm-client-id")
                client_id = val;
        }

        if (!driver.empty() && driver == module) {
            total++;
            SPDLOG_DEBUG(
                "driver = \"{}\", pdev = \"{}\", client_id = \"{}\", client_id_exists = \"{}\"",
                driver, pdev, client_id, client_ids.find(client_id) != client_ids.end()
            );
        }

        if (
            driver.empty() || pdev.empty() || client_id.empty() ||
            driver != module || pdev != pci_dev ||
            client_ids.find(client_id) != client_ids.end()
        )
            continue;

        client_ids.insert(client_id);
        open_fdinfo_fd(fd_path);
    }

    SPDLOG_DEBUG("Found {} total fds. Opened {} unique fds.", total, fdinfo.size());
}

void GPU_fdinfo::open_fdinfo_fd(std::string path) {
    fdinfo.push_back(std::ifstream(path));
    fdinfo_data.push_back({});
}

void GPU_fdinfo::gather_fdinfo_data() {
    for (size_t i = 0; i < fdinfo.size(); i++) {
        fdinfo[i].clear();
        fdinfo[i].seekg(0);

        for (std::string line; std::getline(fdinfo[i], line);) {
            auto key = line.substr(0, line.find(":"));
            auto val = line.substr(key.length() + 2);
            fdinfo_data[i][key] = val;
        }
    }
}

uint64_t GPU_fdinfo::get_gpu_time()
{
    uint64_t total = 0;

    for (auto& fd : fdinfo_data) {
        auto time = fd[drm_engine_type];

        if (time.empty())
            continue;

        total += std::stoull(time);
    }

    return total;
}

float GPU_fdinfo::get_memory_used()
{
    uint64_t total = 0;

    for (auto& fd : fdinfo_data) {
        auto mem = fd[drm_memory_type];

        if (mem.empty())
            continue;

        total += std::stoull(mem);
    }

    // TODO: sometimes it's not KB, so add a check for that.
    return (float)total / 1024 / 1024;
}

void GPU_fdinfo::find_intel_hwmon()
{
    std::string device = "/sys/bus/pci/devices/";
    device += pci_dev;
    device += "/hwmon";

    if (!fs::exists(device)) {
        SPDLOG_DEBUG("Intel hwmon directory {} doesn't exist.", device);
        return;
    }

    auto dir_iterator = fs::directory_iterator(device);
    auto hwmon = dir_iterator->path().string();

    if (hwmon.empty()) {
        SPDLOG_DEBUG("Intel hwmon directory is empty.");
        return;
    }

    hwmon += module == "i915" ? "/energy1_input" : "/energy2_input";

    if (!fs::exists(hwmon)) {
        SPDLOG_DEBUG("Intel hwmon: file {} doesn't exist.", hwmon);
        return;
    }

    SPDLOG_DEBUG("Intel hwmon found: hwmon = {}", hwmon);

    energy_stream.open(hwmon);

    if (!energy_stream.good())
        SPDLOG_DEBUG("Intel hwmon: failed to open {}", hwmon);

    // Initialize value for the first time, otherwise delta will be very large
    // and your gpu power usage will be like 1 million watts for a second.
    this->last_power = get_current_power();
}

float GPU_fdinfo::get_current_power()
{
    if (!energy_stream.is_open())
        return 0.f;

    std::string energy_input_str;
    uint64_t energy_input;

    energy_stream.seekg(0);

    std::getline(energy_stream, energy_input_str);

    if (energy_input_str.empty())
        return 0.f;

    energy_input = std::stoull(energy_input_str);

    return (float)energy_input / 1'000'000;
}

float GPU_fdinfo::get_power_usage()
{
    float now = get_current_power();
    float delta = now - this->last_power;
    delta /= (float)METRICS_UPDATE_PERIOD_MS / 1000;

    this->last_power = now;

    return delta;
}

int GPU_fdinfo::get_xe_load()
{
    double load = 0;

    for (auto& fd : fdinfo_data) {
        std::string client_id = fd["drm-client-id"];
        std::string cur_cycles_str = fd["drm-cycles-rcs"];
        std::string cur_total_cycles_str = fd["drm-total-cycles-rcs"];

        if (
            client_id.empty() || cur_cycles_str.empty() ||
            cur_total_cycles_str.empty()
        )
            continue;

        auto cur_cycles = std::stoull(cur_cycles_str);
        auto cur_total_cycles = std::stoull(cur_total_cycles_str);

        if (prev_xe_cycles.find(client_id) == prev_xe_cycles.end()) {
            prev_xe_cycles[client_id] = { cur_cycles, cur_total_cycles };
            continue;
        }

        auto prev_cycles = prev_xe_cycles[client_id].first;
        auto prev_total_cycles = prev_xe_cycles[client_id].second;

        auto delta_cycles = cur_cycles - prev_cycles;
        auto delta_total_cycles = cur_total_cycles - prev_total_cycles;

        prev_xe_cycles[client_id] = { cur_cycles, cur_total_cycles };

        if (delta_cycles <= 0 || delta_total_cycles <= 0)
            continue;

        auto fd_load = (double)delta_cycles / delta_total_cycles * 100;
        load += fd_load;
    }

    if (load > 100.f)
        load = 100.f;

    return std::lround(load);
}

int GPU_fdinfo::get_gpu_load()
{
    if (module == "xe")
        return get_xe_load();

    uint64_t now = os_time_get_nano();
    uint64_t gpu_time_now = get_gpu_time();

    float delta_time = now - previous_time;
    float delta_gpu_time = gpu_time_now - previous_gpu_time;

    int result = delta_gpu_time / delta_time * 100;

    if (result > 100)
        result = 100;

    previous_gpu_time = gpu_time_now;
    previous_time = now;

    return result;
}

void GPU_fdinfo::main_thread()
{
    while (!stop_thread) {
        std::unique_lock<std::mutex> lock(metrics_mutex);
        cond_var.wait(lock, [this]() { return !paused || stop_thread; });

        gather_fdinfo_data();

        metrics.load = get_gpu_load();
        metrics.memoryUsed = get_memory_used();
        metrics.powerUsage = get_power_usage();

        SPDLOG_DEBUG(
            "pci_dev = {}, pid = {}, module = {}, load = {}, mem = {}, power = {}",
            pci_dev, pid, module, metrics.load, metrics.memoryUsed, metrics.powerUsage
        );

        std::this_thread::sleep_for(
            std::chrono::milliseconds(METRICS_UPDATE_PERIOD_MS)
        );
    }
}
