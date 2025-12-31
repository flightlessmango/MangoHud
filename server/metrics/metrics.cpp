#include "metrics.h"
#include "memory.hpp"
#include <variant>
#include <sys/stat.h>
#include "../../ipc/proto.h"
#include "../common/table_structs.h"

Metrics::Metrics(IPCServer& ipc) : ipc(ipc){
    client_thread     = std::thread(&Metrics::update_client, this);
    table_thread      = std::thread(&Metrics::update_table, this);
    thread            = std::thread(&Metrics::update, this);
    populate_tables_t = std::thread(&Metrics::populate_tables, this);
}

static std::string get_config_dir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (*xdg) return std::string(xdg);
    }
    if (const char* home = std::getenv("HOME")) {
        if (*home) return std::string(home) + "/.config";
    }
    throw std::runtime_error("Cannot determine config directory (no XDG_CONFIG_HOME or HOME)");
}

void Metrics::update_table() {
    for (;;) {
        while (!reload_config()){
            if (stop.load())
                return;

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        };
        std::unique_lock lock(m);
        SPDLOG_INFO("Config changed");
        configPath = get_config_dir() + "/MangoHud/MangoHud.yml";
        table = parse_hud_table(configPath.c_str());
    }
}

void Metrics::update() {
    while (!stop.load()) {
        MetricTable new_metrics;
        for (size_t i = 0; i < gpus.available_gpus.size(); i++) {
            auto& gpu = gpus.available_gpus[i];
            auto gpu_metrics = gpu->get_system_metrics();
            std::string gpu_index = "GPU" + std::to_string(i);
            new_metrics[gpu_index]["LOAD"] = (float)gpu_metrics.load;
            new_metrics[gpu_index]["VRAM_USED"] = gpu_metrics.vram_used;
            new_metrics[gpu_index]["GTT_USED"] = gpu_metrics.gtt_used;
            new_metrics[gpu_index]["VRAM_TOTAL"] = gpu_metrics.memory_total;
            new_metrics[gpu_index]["VRAM_CLOCK"] = (float)gpu_metrics.memory_clock;
            new_metrics[gpu_index]["VRAM_TEMP"] = (float)gpu_metrics.memory_temp;
            new_metrics[gpu_index]["TEMP"] = (float)gpu_metrics.temperature;
            new_metrics[gpu_index]["JUNCTION_TEMP"] = (float)gpu_metrics.junction_temperature;
            new_metrics[gpu_index]["CORE_CLOCK"] = (float)gpu_metrics.core_clock;
            new_metrics[gpu_index]["VOLTAGE"] = (float)gpu_metrics.voltage;
            new_metrics[gpu_index]["POWER"] = gpu_metrics.power_usage;
            new_metrics[gpu_index]["POWER_LIMIT"] = gpu_metrics.power_limit;
            new_metrics[gpu_index]["FAN_SPEED"] = (float)gpu_metrics.fan_speed;
        }

        cpu.poll();
        auto cpu_metrics = cpu.get_info();
        new_metrics["CPU"]["LOAD"] = (float)cpu_metrics.load;
        new_metrics["CPU"]["FREQ"] = (float)cpu_metrics.frequency;
        new_metrics["CPU"]["TEMP"] = (float)cpu_metrics.temp;
        new_metrics["CPU"]["POWER"] = cpu_metrics.power;

        auto ram_metrics = get_ram_info();
        new_metrics["RAM"]["TOTAL"] = ram_metrics["total"];
        new_metrics["RAM"]["USED"] = ram_metrics["used"];
        new_metrics["RAM"]["SWAP_USED"] = ram_metrics["swap_used"];
        {
            std::unique_lock lock(m);
            new_metrics.swap(metrics);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void Metrics::update_client() {
    for (;;) {
        MetricTable new_metrics;
        for (auto& [name, client] : ipc.clients) {
            std::vector<float> frametimes;
            std::unique_lock lock(client->m);
            {
                float avg_fps = client->avg_fps_from_samples();
                new_metrics[name]["FPS"] = avg_fps;
                new_metrics[name]["FRAMETIME"] = 1000.f / avg_fps;
                frametimes.assign(client->frametimes.begin(), client->frametimes.end());
            }
            new_metrics[name]["FRAMETIMES"] = frametimes;

        }
        {
            std::unique_lock lock(m);
            new_metrics.swap(client_metrics);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
    }
}

std::optional<MetricValue> Metrics::get(const char* a, const char* b,
                                        const std::string& name){
    if (!a || !b) {
        SPDLOG_ERROR("Metric query with null key a={} b={}", (const void*)a, (const void*)b);
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(m);

    const auto* outer = &metrics;

    if (std::strcmp(a, "GLOBAL") == 0 && !name.empty()) {
        a = name.c_str();
        outer = &client_metrics;
    }

    if (auto itA = outer->find(a); itA != outer->end()) {
        auto& inner = itA->second;
        if (auto itB = inner.find(b); itB != inner.end())
            return itB->second;
    }

    // SPDLOG_ERROR("Metric does not exist {} {}", a, b);
    return std::nullopt;
}

void Metrics::populate_tables() {
    for (;;) {
        if (table) {
            std::unique_lock clients_lock(ipc.clients_mtx);
            for (auto& [name, client] : ipc.clients) {
                std::unique_lock client_lock(client->m);
                client->table = std::make_shared<HudTable>(
                    assign_values(*table, client->pid, name)
                );
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
    }
}

HudTable Metrics::assign_values(HudTable& t, pid_t& pid, std::string client_name = {}) {
    HudTable render_table;
    render_table.cols = t.cols;
    render_table.rows.reserve(t.rows.size());
    for (auto& row : t.rows) {
        std::vector<MaybeCell> parsed_row;
        parsed_row.reserve(t.cols);
        for (auto& cell : row) {
            TextCell out {};
            if (!cell.has_value()) {
                out.text = " ";
                parsed_row.push_back(out);
                continue;
            }

            Cell& c = *cell;
            if (std::holds_alternative<TextCell>(c)) {
                auto& tc = std::get<TextCell>(c);
                out.vec = color.get(tc.color);
                out.text = tc.text;

                parsed_row.push_back(std::move(out));
                continue;
            }

            if (std::holds_alternative<ValueCell>(c)) {
                auto& vc = std::get<ValueCell>(c);
                out.vec = color.get(vc.color);
                float value = 0;
                std::optional<MetricValue> v =
                    get(vc.ref.a.c_str(), vc.ref.b.c_str(), client_name);
                if (v && std::holds_alternative<float>(*v))
                    value = std::get<float>(*v);

                format_into(out.text, "%.*f", vc.precision, value);
                out.unit = vc.unit;

                parsed_row.push_back(std::move(out));
                continue;
            }

            if (std::holds_alternative<GraphCell>(c)) {
                auto& gc = std::get<GraphCell>(c);
                std::vector<float> data;
                std::optional<MetricValue> v =
                    get(gc.ref.a.c_str(), gc.ref.b.c_str(), client_name);
                if (v && std::holds_alternative<std::vector<float>>(*v))
                    out.data = std::get<std::vector<float>>(*v);

                parsed_row.push_back(std::move(out));
                continue;
            }

        }
        render_table.rows.push_back(std::move(parsed_row));
    }

    return render_table;
}

bool Metrics::read_sig(const char* path, configSig& out) {
    struct stat st {};
    if (::stat(path, &st) == 0) {
        out.exists = true;
        out.size = static_cast<std::int64_t>(st.st_size);
        out.sec  = static_cast<std::int64_t>(st.st_mtim.tv_sec);
        out.nsec = static_cast<std::int64_t>(st.st_mtim.tv_nsec);
        return true;
    }

    if (errno == ENOENT || errno == ENOTDIR) {
        out = configSig{};
        return true;
    }

    out = configSig{};
    return false;
}

bool Metrics::sig_changed(const configSig& a, const configSig& b) {
    return a.exists != b.exists ||
        a.size   != b.size   ||
        a.sec    != b.sec    ||
        a.nsec   != b.nsec;
}
bool Metrics::reload_config() {
    if (configPath.empty())
        return false;

    configSig now;
    read_sig(configPath.c_str(), now);
    if (sig_changed(previousSig, now)) {
        previousSig = now;
        return true;
    }
    return false;
}

void Metrics::format_into(std::string& dst, const char* fmt, ...) const {
  va_list ap;
  va_start(ap, fmt);

  std::array<char, 256> stack{};

  va_list ap2;
  va_copy(ap2, ap);
  int n = std::vsnprintf(stack.data(), stack.size(), fmt, ap2);
  va_end(ap2);

  if (n < 0) {
    dst.clear();
    va_end(ap);
    return;
  }

  if ((size_t)n < stack.size()) {
    dst.assign(stack.data(), (size_t)n);
    va_end(ap);
    return;
  }

  dst.resize((size_t)n);
  std::vsnprintf(dst.data(), dst.size() + 1, fmt, ap);
  va_end(ap);
}
