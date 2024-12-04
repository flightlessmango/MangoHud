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

    if (module == "xe")
        xe_fdinfo_last_cycles.push_back(0);
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

std::pair<uint64_t, uint64_t> GPU_fdinfo::get_gpu_time_xe()
{
    uint64_t total_cycles = 0, total_total_cycles = 0;

    size_t idx = -1;
    for (auto& fd : fdinfo_data) {
        idx++;

        auto cur_cycles_str = fd["drm-cycles-rcs"];
        auto cur_total_cycles_str = fd["drm-total-cycles-rcs"];

        if (cur_cycles_str.empty() || cur_total_cycles_str.empty())
            continue;

        auto cur_cycles = std::stoull(cur_cycles_str);
        auto cur_total_cycles = std::stoull(cur_total_cycles_str);

        if (
            cur_cycles <= 0 ||
            cur_cycles == xe_fdinfo_last_cycles[idx] ||
            cur_total_cycles <= 0
        )
            continue;

        total_cycles += cur_cycles;
        total_total_cycles += cur_total_cycles;

        xe_fdinfo_last_cycles[idx] = cur_cycles;
    }

    return { total_cycles, total_total_cycles };
}

int GPU_fdinfo::get_xe_load()
{
    static uint64_t previous_cycles, previous_total_cycles;

    auto gpu_time = get_gpu_time_xe();
    uint64_t cycles = gpu_time.first;
    uint64_t total_cycles = gpu_time.second;

    uint64_t delta_cycles = cycles - previous_cycles;
    uint64_t delta_total_cycles = total_cycles - previous_total_cycles;

    if (delta_cycles == 0 || delta_total_cycles == 0)
        return 0;

    double load = (double)delta_cycles / delta_total_cycles * 100;

    if (load > 100.f)
        load = 100.f;

    previous_cycles = cycles;
    previous_total_cycles = total_cycles;

    // SPDLOG_DEBUG("cycles             = {}", cycles);
    // SPDLOG_DEBUG("total_cycles       = {}", total_cycles);
    // SPDLOG_DEBUG("delta_cycles       = {}", delta_cycles);
    // SPDLOG_DEBUG("delta_total_cycles = {}", delta_total_cycles);
    // SPDLOG_DEBUG("{} / {} * 100 = {}", delta_cycles, delta_total_cycles, load);
    // SPDLOG_DEBUG("load = {}\n", std::lround(load));

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
