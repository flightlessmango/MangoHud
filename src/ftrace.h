#pragma once

#ifdef HAVE_FTRACE

#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include "overlay_params.h"

namespace FTrace {

enum class TracepointType {
    Histogram,
    LineGraph,
    Label,
};

struct Tracepoint {
    uint32_t update_index { 0 };
    std::string name;
    TracepointType type;
    std::string field_name;

    static constexpr unsigned PLOT_DATA_CAPACITY = 200;

    struct CollectionValue {
        float f { 0 };
        std::string field_value;
    };

    struct {
        struct {
            std::array<float, PLOT_DATA_CAPACITY> values;
            struct {
                float min { 0 };
                float max { 0 };
            } range;
        } plot;
        std::string field_value;
    } data;
};

class FTrace {
private:
    int trace_pipe_fd { -1 };

    std::thread thread;
    std::atomic<bool> stop_thread { false };

    void ftrace_thread();
    void handle_ftrace_entry(std::string entry);

    struct {
        std::mutex mutex;
        std::unordered_map<std::shared_ptr<Tracepoint>, Tracepoint::CollectionValue> map;
    } collection;

    struct {
        std::vector<std::shared_ptr<Tracepoint>> tracepoints;
        uint32_t update_index { 0 };
    } data;

public:
    FTrace(const overlay_params::ftrace_options& options);
    ~FTrace();

    void update();

    const std::vector<std::shared_ptr<Tracepoint>>& tracepoints() { return data.tracepoints; }

    static float get_plot_values(void *data, int index);
};

extern std::unique_ptr<FTrace> object;

} // namespace FTrace

#endif // HAVE_FTRACE
