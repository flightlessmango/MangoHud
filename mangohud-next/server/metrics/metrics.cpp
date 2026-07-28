#include "metrics.h"
#include "memory.hpp"
#include <variant>
#include <string_view>
#include <cctype>
#include <algorithm>
#include <sys/stat.h>
#include <spdlog/fmt/bundled/format.h>
#include "../common/json.h"
#include "../common/table_structs.h"
#include "../common/helpers.hpp"
#include "string_utils.h"

static void replace_all(std::string& text, std::string_view from, std::string_view to) {
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static std::string left_pad(std::string text, std::size_t width) {
    if (text.size() >= width)
        return text;

    return std::string(width - text.size(), ' ') + text;
}

static void truncate_text(std::string& text, int max_chars) {
    if (max_chars <= 0 || text.size() <= static_cast<std::size_t>(max_chars))
        return;

    if (max_chars <= 3)
        text.resize(max_chars);
    else {
        text.resize(max_chars - 3);
        text += "...";
        return;
    }
}

static bool parse_gpu_index(const char* key, size_t& index) {
    if (!key || std::strncmp(key, "GPU", 3) != 0)
        return false;

    const char* p = key + 3;
    if (!std::isdigit(static_cast<unsigned char>(*p)) || p[1] != '\0')
        return false;

    index = static_cast<size_t>(*p - '0');
    return true;
}

static std::string metrics_to_json(const MetricTable::mapped_type& metrics) {
    std::string out;

    out.push_back('{');
    bool first = true;
    for (const auto& [key, metric] : metrics) {
        if (!first)
            out.push_back(',');
        first = false;

        append_json_string(out, key);
        out += R"(:{"value":)";

        if (metric.val) {
            std::visit([&out](const auto& value) {
                append_json_value(out, value);
            }, *metric.val);
        } else {
            out += "null";
        }

        if (!metric.unit.empty()) {
            out += R"(,"unit":)";
            append_json_string(out, metric.unit);
        }

        out.push_back('}');
    }

    out.push_back('}');
    return out;
}

Metrics::Metrics(IPCServer& ipc, std::shared_ptr<Config> cfg_) : cfg(cfg_), ipc(ipc) {
    client_thread     = std::thread(&Metrics::update_client, this);
    pthread_setname_np(client_thread.native_handle(), "update_client");
    thread            = std::thread(&Metrics::update, this);
    pthread_setname_np(thread.native_handle(), "update_metrics");
}

void Metrics::update() {
    while (!stop.load()) {
        MetricTable new_metrics;
        const auto now = std::chrono::steady_clock::now();
        for (const auto& [i, gpu] : enumerate(gpus.available())) {
            gpu->stop_polling_if_idle(now, std::chrono::seconds(3));
            if (!gpu->polling_active())
                continue;

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
            new_metrics[gpu_index]["VOLTAGE"] = {gpu_metrics.voltage, "mV"};
            new_metrics[gpu_index]["POWER"] = {(int)gpu_metrics.power_usage, "W"};
            new_metrics[gpu_index]["POWER_LIMIT"] = {(int)gpu_metrics.power_limit, "W"};
            new_metrics[gpu_index]["FAN_SPEED"] = {gpu_metrics.fan_speed, gpu_metrics.fan_rpm ? "RPM" : "%"};
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
                std::vector<float> output_frametimes;
                std::vector<float> hud_frametimes;
                float avg_fps;
                float output_fps;
                float hud_fps;
                {
                    std::lock_guard lock(client->m);
                    frametimes = client->stats_for(SampleType::Frame).frametimes_copy();
                    output_frametimes = client->stats_for(SampleType::Output).frametimes_copy();
                    hud_frametimes = client->stats_for(SampleType::Hud).frametimes_copy();
                    avg_fps = client->stats_for(SampleType::Frame).avg_fps();
                    output_fps = client->stats_for(SampleType::Output).avg_fps();
                    hud_fps = client->stats_for(SampleType::Hud).avg_fps();
                    auto& metrics = new_metrics[std::to_string(client->pid)];
                    metrics["ENGINE_NAME"] = {engine_name(client->pEngineName)};
                    metrics["GPU_NAME"] = {client->gpuName};
                    metrics["VULKAN_DRIVER"] = {client->vulkanDriver};
                    metrics["FOCUSED"] = {client->focused() ? "true" : "false"};
                    metrics["FOCUSED_SEATS"] = {fmt::format("{}", fmt::join(client->focused_seats, ","))};
                    if (client->resolutionWidth && client->resolutionHeight)
                        metrics["RESOLUTION"] = {std::to_string(client->resolutionWidth) + "x" + std::to_string(client->resolutionHeight)};
                }
                // TODO fps and frametime updates should match other metrics at 500ms
                // frametimes should still be this fast
                new_metrics[std::to_string(client->pid)]["FPS"] = {int(round(avg_fps)), "FPS"};
                new_metrics[std::to_string(client->pid)]["OUTPUT_FPS"] = {int(round(output_fps)), "FPS"};
                new_metrics[std::to_string(client->pid)]["HUD_FPS"] = {int(round(hud_fps)), "FPS"};
                new_metrics[std::to_string(client->pid)]["FRAMETIME"] = {1000.f / avg_fps, "ms"};
                new_metrics[std::to_string(client->pid)]["OUTPUT_FRAMETIME"] = {output_fps > 0 ? 1000.f / output_fps : 0.f, "ms"};
                new_metrics[std::to_string(client->pid)]["HUD_FRAMETIME"] = {hud_fps > 0 ? 1000.f / hud_fps : 0.f, "ms"};
                new_metrics[std::to_string(client->pid)]["FRAMETIMES"] = {frametimes};
                new_metrics[std::to_string(client->pid)]["OUTPUT_FRAMETIMES"] = {output_frametimes};
                new_metrics[std::to_string(client->pid)]["HUD_FRAMETIMES"] = {hud_frametimes};
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

std::string Metrics::system_json_snapshot() {
    std::lock_guard lock(m);

    std::vector<std::string> groups;
    groups.reserve(metrics.size());
    for (const auto& group : metrics)
        groups.push_back(group.first);

    std::sort(groups.begin(), groups.end());
    std::unordered_map<std::string, bool> gpu_polling;
    for (const auto& [i, gpu] : enumerate(gpus.available()))
        gpu_polling["GPU" + std::to_string(i)] = gpu->polling_active();

    std::string out;
    out += "{\"system\":{";

    bool first = true;
    for (const auto& group : groups) {
        if (!first)
            out.push_back(',');
        first = false;
        append_json_string(out, group);
        out += ":{";
        if (auto it = gpu_polling.find(group); it != gpu_polling.end()) {
            out += "\"polling\":";
            append_json_value(out, it->second);
            out.push_back(',');
        }
        out += "\"metrics\":";
        out += metrics_to_json(metrics.at(group));
        out.push_back('}');
    }

    out += "}}";
    return out;
}

std::string Metrics::clients_json_snapshot() {
    std::lock_guard lock(m);

    std::vector<const MetricTable::value_type*> client_entries;
    client_entries.reserve(client_metrics.size());
    for (const auto& client : client_metrics)
        client_entries.push_back(&client);

    std::sort(client_entries.begin(), client_entries.end(), [](const auto* a, const auto* b) {
        return std::stoll(a->first) < std::stoll(b->first);
    });

    std::string out;
    out += "{\"clients\":[";
    for (const auto* client : client_entries) {
        if (client != client_entries.front())
            out.push_back(',');
        out += "{\"pid\":";
        append_json_value(out, std::stoll(client->first));
        out += ",\"metrics\":";
        out += metrics_to_json(client->second);
        out.push_back('}');
    }
    out += "]}";

    return out;
}

Metric Metrics::get(const char* a, const char* b, const pid_t pid = 0)
{
    Metric null_out;
    if (!a || !b) {
        SPDLOG_ERROR("Metric query with null key a={} b={}", (const void*)a, (const void*)b);
        return null_out;
    }

    if (std::strcmp(a, "GLOBAL") == 0) {
        if (const char* command = Exec::value_command(to_uppercase(b))) {
            std::string resolved_command = command;
            replace_all(resolved_command, "{pid}", std::to_string(pid));

            auto [valid, text] = exec.get(resolved_command);
            if (valid)
                return {std::move(text)};

            return null_out;
        }
    }

    size_t gpu_index = 0;
    if (parse_gpu_index(a, gpu_index)) {
        auto available_gpus = gpus.available();
        if (gpu_index < available_gpus.size())
            available_gpus[gpu_index]->request_polling();
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
    if (cfg->hud) {
        HudConfig local;
        {
            std::lock_guard lock(cfg->m);
            local = *cfg->hud;
        }
        std::unordered_map<pid_t, std::shared_ptr<clientRes>> client_res;
        {
            std::lock_guard lock(ipc.clients_mtx);
            for (auto client : ipc.clients)
                client_res.emplace(client->pid, client->resources);
        }

        {
            for (auto& [pid, r] : client_res) {
                std::lock_guard lock(r->hud_m);
                r->hud->windows.clear();
                r->hud->windows.reserve(local.windows.size());
                for (auto& window : local.windows) {
                    HudWindow out;
                    out.background = window.background;
                    out.padding = window.padding;
                    out.position = window.position;
                    assign_values(&window.table, pid, &out.table);
                    r->hud->windows.push_back(std::move(out));
                }
            }
        }
    }
}

void Metrics::assign_values(hudTable* t, pid_t pid, hudTable* render_table) {
    render_table->rows.clear();
    render_table->cols = t->cols;
    render_table->font_size = t->font_size;
    render_table->col_gap = t->col_gap;
    render_table->row_gap = t->row_gap;
    render_table->debug_cell_boxes = t->debug_cell_boxes;
    render_table->rows.reserve(t->rows.size());
    for (auto& row : t->rows) {
        std::vector<MaybeCell> parsed_row;
        parsed_row.reserve(t->cols);
        for (auto& cell : row) {
            TextCell out {};
            if (!cell.has_value()) {
                parsed_row.push_back(std::nullopt);
                continue;
            }

            Cell& c = *cell;
            if (std::holds_alternative<TextCell>(c)) {
                auto& tc = std::get<TextCell>(c);
                out.vec = color.get(tc.color);
                out.text = tc.text;
                out.style = tc.style;
                truncate_text(out.text, out.style.truncate);

                parsed_row.push_back(std::move(out));
                continue;
            }

            if (std::holds_alternative<ValueCell>(c)) {
                auto& vc = std::get<ValueCell>(c);
                out.vec = color.get(vc.color);
                out.style = vc.style;
                float value = 0;
                int i_value = 0;
                Metric metric = get(vc.ref.a.c_str(), vc.ref.b.c_str(), pid);
                if (!metric.val) {
                    if (vc.unit_override)
                        out.unit = vc.unit;
                    parsed_row.push_back(std::move(out));
                    continue;
                }

                if (metric.val && std::holds_alternative<std::string>(*metric.val))
                    out.text = std::get<std::string>(*metric.val);

                if (metric.val && std::holds_alternative<float>(*metric.val)) {
                    value = std::get<float>(*metric.val);
                    if (vc.unit_override)
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
                    if (vc.unit_override)
                        out.unit = vc.unit;
                    else
                        out.unit = metric.unit;

                    format_into(out.text, "%i", i_value);
                }

                truncate_text(out.text, out.style.truncate);
                parsed_row.push_back(std::move(out));
                continue;
            }

            if (std::holds_alternative<GraphCell>(c)) {
                auto& gc = std::get<GraphCell>(c);
                std::vector<float> data;
                out.style = gc.style;
                Metric metric = get(gc.ref.a.c_str(), gc.ref.b.c_str(), pid);
                if (metric.val && std::holds_alternative<std::vector<float>>(*metric.val))
                    out.data = std::get<std::vector<float>>(*metric.val);

                parsed_row.push_back(std::move(out));
                continue;
            }

            if (std::holds_alternative<ProgressCell>(c)) {
                auto& pc = std::get<ProgressCell>(c);

                auto metric_float = [&](const MetricRef& ref, float fallback, std::string* unit = nullptr) -> float {
                    Metric metric = get(ref.a.c_str(), ref.b.c_str(), pid);
                    if (!metric.val)
                        return fallback;

                    if (unit && unit->empty())
                        *unit = metric.unit;

                    if (std::holds_alternative<float>(*metric.val))
                        return std::get<float>(*metric.val);

                    if (std::holds_alternative<int>(*metric.val))
                        return static_cast<float>(std::get<int>(*metric.val));

                    return fallback;
                };

                auto resolve_bound = [&](const ProgressBound& bound, float fallback) -> float {
                    if (std::holds_alternative<float>(bound))
                        return std::get<float>(bound);

                    return metric_float(std::get<MetricRef>(bound), fallback);
                };

                ProgressCell progress = pc;
                progress.unit = pc.unit;
                progress.value = metric_float(pc.ref, 0.0f, pc.unit_override ? nullptr : &progress.unit);
                progress.min_value = resolve_bound(pc.min, 0.0f);
                progress.max_value = resolve_bound(pc.max, 100.0f);
                progress.vec = color.get(pc.color);
                progress.background_vec = color.get(pc.background_color);

                if (!pc.text.empty()) {
                    auto formatted = [&](float value) {
                        std::string out;
                        format_into(out, "%.*f", pc.precision, value);
                        return out;
                    };

                    const float range = progress.max_value - progress.min_value;
                    const float normalized = range == 0.0f ? 0.0f : (progress.value - progress.min_value) / range;
                    const std::string value = formatted(progress.value);
                    const std::string min = formatted(progress.min_value);
                    const std::string max = formatted(progress.max_value);
                    const std::string percent = formatted(normalized * 100.0f);
                    const std::string reserve_value = progress.unit == "%" ? "100" : (min.size() > max.size() ? min : max);
                    const std::string reserve_percent = "100";

                    auto render_text = [&](const std::string& value_text, const std::string& percent_text) {
                        std::string text = pc.text;
                        replace_all(text, "{value}", value_text);
                        replace_all(text, "{min}", min);
                        replace_all(text, "{max}", max);
                        replace_all(text, "{percent}", percent_text);
                        replace_all(text, "{unit}", progress.unit);
                        return text;
                    };

                    progress.text = render_text(left_pad(value, reserve_value.size()), left_pad(percent, reserve_percent.size()));
                    progress.layout_text = render_text(reserve_value, reserve_percent);
                    truncate_text(progress.text, progress.style.truncate);
                    truncate_text(progress.layout_text, progress.style.truncate);
                }

                parsed_row.push_back(Cell{std::move(progress)});
                continue;
            }

            if (std::holds_alternative<ExecCell>(c)) {
                auto& ec = std::get<ExecCell>(c);
                auto [valid, text] = exec.get(ec.command);
                out.vec = color.get(ec.color);
                if (valid)
                    out.text = std::move(text);
                out.unit = ec.unit;
                out.style = ec.style;
                truncate_text(out.text, out.style.truncate);

                parsed_row.push_back(std::move(out));
                continue;
            }

            if (std::holds_alternative<SeparatorCell>(c)) {
                auto& sc = std::get<SeparatorCell>(c);
                SeparatorCell out_separator = sc;
                out_separator.vec = color.get(sc.color);
                parsed_row.push_back(Cell{std::move(out_separator)});
                continue;
            }

            if (std::holds_alternative<TableCell>(c)) {
                auto& tc = std::get<TableCell>(c);
                if (!tc.table)
                    continue;

                TableCell out_table;
                out_table.table = std::make_shared<hudTable>();
                out_table.style = tc.style;
                assign_values(tc.table.get(), pid, out_table.table.get());
                parsed_row.push_back(Cell{std::move(out_table)});
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
