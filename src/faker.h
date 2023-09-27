#include <iostream>
#include <random>
#include <cmath>
#include "overlay.h"
#include "gpu.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <random>

class SmoothNumberGenerator {
    private:
        std::default_random_engine generator;
        std::uniform_real_distribution<float> distribution;
        float previous;
        float minRange, maxRange;
        float delta;

    public:
        SmoothNumberGenerator(float min = 16.3f, float max = 17.4f, float optional_delta = 1.f)
            : distribution(min, max),
            minRange(min),
            maxRange(max),
            delta(optional_delta) {
            unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
            generator.seed(seed);
            previous = distribution(generator);
        }

        float next() {
            float lower_bound = std::max(minRange, previous - delta);
            float upper_bound = std::min(maxRange, previous + delta);

            // We temporarily adjust the distribution's parameters to generate within our desired range
            distribution.param(std::uniform_real_distribution<float>::param_type(lower_bound, upper_bound));
            previous = distribution(generator);

            return previous;
        }
};

class fakeCPU {
    public:


};

class fakeGPU {
    private:
        SmoothNumberGenerator gpuLoad;
        SmoothNumberGenerator fanSpeed;
        SmoothNumberGenerator temp;
        SmoothNumberGenerator powerUsage;

    public:
        fakeGPU() : gpuLoad(90, 95),
                    fanSpeed(2000, 2500),
                    temp(85,90),
                    powerUsage(150,200) {}

        void update() {
            gpuInfo gpu;
            gpu.load = gpuLoad.next();
            gpu.CoreClock = 2800;
            gpu.fan_speed = fanSpeed.next();
            gpu.is_current_throttled = false;
            gpu.is_other_throttled = false;
            gpu.is_power_throttled = false;
            gpu.is_temp_throttled = true;
            gpu.MemClock = 1250;
            gpu.memory_temp = temp.next();
            gpu.memoryUsed = 3242;
            gpu.powerUsage = powerUsage.next();
            gpu.temp = temp.next();

            gpu_info = gpu;
        };

};

class Faker {
    private:
        swapchain_stats sw_stats;
        overlay_params params;
        uint32_t vendorID;
        fakeGPU gpu;

    public:
        Faker(swapchain_stats sw_stats, overlay_params params, uint32_t vendorID)
            : sw_stats(sw_stats), params(params), vendorID(vendorID) {}

        void update_frametime() {
            update_hud_info_with_frametime(sw_stats, params, vendorID, 16.6f);
        }

        void update() {
            // update_frametime();
            gpu.update();
        }

        uint64_t now() {
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
            auto ret = duration.count();
            return ret;
        }
};

extern std::unique_ptr<Faker> faker;