#pragma once
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <mesa/util/os_time.h>
#include <numeric>
#include <mutex>
#include <algorithm>
#include <condition_variable>
#include <stdexcept>
#include <iomanip>
#include <spdlog/spdlog.h>

struct metric_t {
    std::string name;
    float value;
    std::string display_name;
};

class fpsMetrics {
    private:
        std::vector<float> frametimes;
        std::thread thread;
        std::mutex mtx;
        std::condition_variable cv;
        bool run = false;
        bool thread_init = false;
        bool terminate = false;
        bool resetting = false;
        size_t max_size = 10000;
        std::vector<metric_t> metrics;

        void _thread() {
            thread_init = true;
            while (true){
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait(lock, [this] { return run; });

                if (terminate)
                    break;

                calculate();

                run = false;
            }
        }

        void calculate(){
            if (frametimes.empty())
                return; 

            std::vector<float> sorted_values = frametimes;
            std::sort(sorted_values.begin(), sorted_values.end(), std::greater<float>());

            auto it = metrics.begin();
            while (it != metrics.end()) {
                if (it->name == "AVG") {
                    it->display_name = it->name;

                    float sum = 0.0f;
                    for (const auto& f : sorted_values)
                        sum += f;

                    float avg = 1000.f / (sum / sorted_values.size());
                    it->value = avg;
                } else {
                    try {
                        float val = std::stof(it->name);
                        if (val <= 0.0f || val >= 1.0f) {
                            SPDLOG_DEBUG("Failed to use fps metric, it's out of range {}", it->name);
                            it = metrics.erase(it);
                            continue;
                        }

                        // Format display name as a percentage
                        float multiplied_val = val * 100;
                        std::ostringstream stream;
                        stream << std::fixed << std::setprecision(multiplied_val == static_cast<int>(multiplied_val) ? 0 : 1)
                               << multiplied_val << "%";
                        it->display_name = stream.str();
                        uint64_t idx = val * sorted_values.size() - 1;
                        if (idx >= sorted_values.size())
                            break;

                        it->value = 1000.f / sorted_values[idx];
                    } catch (const std::invalid_argument& e) {
                        SPDLOG_DEBUG("Failed to use fps metric value {}", it->name);
                        it = metrics.erase(it);
                        continue;
                    }
                }
                ++it;
            }
        }

        std::vector<metric_t> add_metrics_to_vector(std::vector<std::string> values) {
            std::vector<metric_t> _metrics;
            for (auto& val : values){
                for(char& c : val) {
                    c = std::toupper(static_cast<unsigned char>(c));
                }
                _metrics.push_back({val, 0.0f});
            }
            return _metrics;
        }

    public:
        fpsMetrics(std::vector<std::string> values){
            metrics = add_metrics_to_vector(values);

            if (!thread_init) {
                thread = std::thread(&fpsMetrics::_thread, this);
                // "mangohud-fpsmetrics" wouldn't fit in the 15 byte limit
                pthread_setname_np(thread.native_handle(), "mangohud-fpsmet");
            }
        };

        fpsMetrics(std::vector<std::string> values, std::vector<float> only_frametime) {
            metrics = add_metrics_to_vector(values);
            for (auto& frametime : only_frametime)
                frametimes.push_back(frametime);

            calculate();
        };

        void update(float new_frametime) {
            if (resetting)
                return;

            if (new_frametime > 100000) return; // Ignore extremely long frames

            // lock before modifying vector
            std::lock_guard<std::mutex> lock(mtx);

            if (frametimes.size() >= max_size)
                frametimes.erase(frametimes.begin());

            frametimes.push_back(new_frametime);
        }


        void update_thread(){
            if (resetting)
                return;

            {
                std::lock_guard<std::mutex> lock(mtx);
                run = true;
            }
            cv.notify_one();
        }

        void reset_metrics(){
            resetting = true;
            while (run){}
            frametimes.clear();
            resetting = false;
        }

        std::vector<metric_t> copy_metrics() {
            std::lock_guard<std::mutex> lock(mtx);
            return metrics;
        }

        ~fpsMetrics(){
            terminate = true;
            {
                std::lock_guard<std::mutex> lock(mtx);
                run = true;
            }
            cv.notify_one();
            if (thread.joinable())
                thread.join();
        }
};

extern std::unique_ptr<fpsMetrics> fpsmetrics;
