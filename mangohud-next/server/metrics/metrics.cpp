#include "metrics.h"
#include "memory.hpp"
#include <variant>
#include <sys/stat.h>
#include "../common/table_structs.h"
#include "string_utils.h"

Metrics::Metrics(IPCServer& ipc, std::shared_ptr<Config> cfg_) : cfg(cfg_), ipc(ipc) {
    client_thread     = std::thread(&Metrics::update_client, this);
    pthread_setname_np(client_thread.native_handle(), "update_client");
    thread            = std::thread(&Metrics::update, this);
    pthread_setname_np(thread.native_handle(), "update_metrics");
}

void Metrics::update() {
    while (!stop.load()) {
        MetricTable new_metrics;
        for (size_t i = 0; i < gpus.available_gpus.size(); i++) {
            auto& gpu = gpus.available_gpus[i];
            auto gpu_metrics = gpu->get_system_metrics();
            std::string gpu_index = "GPU" + std::to_string(i);
            new_metrics[gpu_index]["LOAD"] = {gpu_metrics.load, "%"};
            new_metrics[gpu_index]["VRAM_USED"] = {gpu_metrics.vram_used, "GiB"};
            new_metrics[gpu_index]["GTT_USED"] = {gpu_metrics.gtt_used, "GiB"};
            new_metrics[gpu_index]["VRAM_TOTAL"] = {gpu_metrics.memory_total, "GiB"};
            new_metrics[gpu_index]["VRAM_CLOCK"] = {gpu_metrics.memory_clock, "MHz"};
            new_metrics[gpu_index]["VRAM_TEMP"] = {gpu_metrics.memory_temp, "°C"};
            new_metrics[gpu_index]["TEMP"] = {gpu_metrics.temperature, "°C"};
            new_metrics[gpu_index]["JUNCTION_TEMP"] = {gpu_metrics.junction_temperature, "°C"};
            new_metrics[gpu_index]["CORE_CLOCK"] = {gpu_metrics.core_clock, "MHz"};
            new_metrics[gpu_index]["VOLTAGE"] = {gpu_metrics.voltage, "W"};
            new_metrics[gpu_index]["POWER"] = {(int)gpu_metrics.power_usage, "W"};
            new_metrics[gpu_index]["POWER_LIMIT"] = {gpu_metrics.power_limit, "W"};
            new_metrics[gpu_index]["FAN_SPEED"] = {gpu_metrics.fan_speed, "%"};
        }

        cpu.poll();
        auto cpu_metrics = cpu.get_info();
        new_metrics["CPU"]["LOAD"] = {cpu_metrics.load, "%"};
        new_metrics["CPU"]["FREQ"] = {cpu_metrics.frequency, "MHz"};
        new_metrics["CPU"]["TEMP"] = {cpu_metrics.temp, "°C"};
        new_metrics["CPU"]["POWER"] = {(int)cpu_metrics.power, "W"};

        auto ram_metrics = get_ram_info();
        for (auto& [k, v] : ram_metrics)
            new_metrics["RAM"][to_uppercase(k)] = {v, "GiB"};

        {
            std::lock_guard lock(m);
            new_metrics.swap(metrics);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void Metrics::update_client() {
    while (!stop.load()) {
        MetricTable new_metrics;
        {
            std::lock_guard clients_lock(ipc.clients_mtx);
            for (auto client : ipc.clients) {
                std::vector<float> frametimes;
                float avg_fps;
                {
                    std::lock_guard lock(client->m);
                    frametimes.assign(client->frametimes.begin(), client->frametimes.end());
                    avg_fps = client->avg_fps_from_samples();
                    new_metrics[std::to_string(client->pid)]["ENGINE_NAME"] = {engine_name(client->pEngineName)};
                }
                // TODO fps and frametime updates should match other metrics at 500ms
                // frametimes should still be this fast
                new_metrics[std::to_string(client->pid)]["FPS"] = {int(round(avg_fps)), "FPS"};
                new_metrics[std::to_string(client->pid)]["FRAMETIME"] = {1000.f / avg_fps, "ms"};
                new_metrics[std::to_string(client->pid)]["FRAMETIMES"] = {frametimes};
            }
        }

        {
            std::lock_guard lock(m);
            new_metrics.swap(client_metrics);
        }
        populate_tables();
        std::this_thread::sleep_for(std::chrono::milliseconds(7));
    }
}

Metric Metrics::get(const char* a, const char* b, const pid_t pid = 0)
{
    Metric null_out;
    if (!a || !b) {
        SPDLOG_ERROR("Metric query with null key a={} b={}", (const void*)a, (const void*)b);
        return null_out;
    }

    std::lock_guard<std::mutex> lock(m);

    const auto* outer = &metrics;
    std::string str = std::to_string(pid);
    if (std::strcmp(a, "GLOBAL") == 0 && pid > 0) {
        a = str.c_str();
        outer = &client_metrics;
    }

    if (auto itA = outer->find(a); itA != outer->end()) {
        auto& inner = itA->second;
        if (auto itB = inner.find(b); itB != inner.end())
            return itB->second;
    }

    // TODO Add ram temp
    // SPDLOG_ERROR("Metric does not exist {} {}", a, b);
    return null_out;
}

void Metrics::populate_tables() {
    if (cfg->table) {
        hudTable local;
        {
            std::lock_guard lock(cfg->m);
            local = *cfg->table;
        }
            std::unordered_map<pid_t, std::shared_ptr<clientRes>> client_res;
        {
            std::lock_guard lock(ipc.clients_mtx);
            for (auto client : ipc.clients)
                client_res.emplace(client->pid, client->resources);
        }

        {
            for (auto& [pid, r] : client_res) {
                std::lock_guard lock(r->table_m);
                assign_values(&local, pid, r->table.get());
            }
        }
    }
}

void Metrics::assign_values(hudTable* t, pid_t pid, hudTable* render_table) {
    render_table->rows.clear();
    render_table->cols = t->cols;
    render_table->rows.reserve(t->rows.size());
    for (auto& row : t->rows) {
        std::vector<MaybeCell> parsed_row;
        parsed_row.reserve(t->cols);
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
                int i_value = 0;
                Metric metric = get(vc.ref.a.c_str(), vc.ref.b.c_str(), pid);
                if (!metric.val)
                    continue;

                if (metric.val && std::holds_alternative<std::string>(*metric.val))
                    out.text = std::get<std::string>(*metric.val);

                if (metric.val && std::holds_alternative<float>(*metric.val)) {
                    value = std::get<float>(*metric.val);
                    if (!vc.unit.empty())
                        out.unit = vc.unit;
                    else
                        out.unit = metric.unit;

                    if (!vc.precision)
                        format_into(out.text, "%.*f", 1, value);
                    else
                        format_into(out.text, "%.*f", vc.precision, value);
                }

                if (metric.val && std::holds_alternative<int>(*metric.val)) {
                    i_value = std::get<int>(*metric.val);
                    if (!vc.unit.empty())
                        out.unit = vc.unit;
                    else
                        out.unit = metric.unit;

                    format_into(out.text, "%i", i_value);
                }

                parsed_row.push_back(std::move(out));
                continue;
            }

            if (std::holds_alternative<GraphCell>(c)) {
                auto& gc = std::get<GraphCell>(c);
                std::vector<float> data;
                Metric metric = get(gc.ref.a.c_str(), gc.ref.b.c_str(), pid);
                if (metric.val && std::holds_alternative<std::vector<float>>(*metric.val))
                    out.data = std::get<std::vector<float>>(*metric.val);

                parsed_row.push_back(std::move(out));
                continue;
            }

        }
        render_table->rows.push_back(std::move(parsed_row));
    }
}

void Metrics::format_into(std::string& dst, const char* fmt, ...) const {
    va_list ap;
    va_start(ap, fmt);

    va_list ap2;
    va_copy(ap2, ap);
    int n = std::vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);

    if (n < 0) {
        dst.clear();
        va_end(ap);
        return;
    }

    dst.resize(static_cast<size_t>(n) + 1);
    std::vsnprintf(dst.data(), static_cast<size_t>(n) + 1, fmt, ap);
    dst.resize(static_cast<size_t>(n));

    va_end(ap);
}

std::string Metrics::engine_name(const std::string& engine)  {
    if (engine == "DXVK")       return "DXVK";
    if (engine == "vkd3d")      return "VKD3D";
    if (engine == "mesa zink")  return "ZINK";
    if (engine == "Damavand")   return "DAMAVAND";
    if (engine == "Feral3D")    return "Feral3D";
    if (engine == "OpenGL")     return "OpenGL";
    if (engine == "WINED3D")    return "WINED3D";
    return "VULKAN";
}
