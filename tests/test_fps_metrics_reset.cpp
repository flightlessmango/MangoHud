#include "../src/fps_metrics.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

#define REQUIRE(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
    std::abort(); \
} } while (0)

static void wait_for_average(fpsMetrics& metrics, float expected)
{
    for (int i = 0; i < 100; ++i) {
        const auto snapshot = metrics.copy_metrics();
        if (std::abs(snapshot[0].value - expected) < 0.1f) {
            REQUIRE(std::abs(snapshot[1].value - expected) < 0.1f);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(false && "metrics worker did not calculate in time");
}

int main()
{
    fpsMetrics short_sample({"0.01"}, {20.0f});
    REQUIRE(std::abs(short_sample.copy_metrics()[0].value - 50.0f) < 0.1f);

    fpsMetrics metrics({"avg", "0.01"});
    for (int i = 0; i < 200; ++i)
        metrics.update(20.0f);
    metrics.update_thread();
    wait_for_average(metrics, 50.0f);

    metrics.reset_metrics();
    for (const auto& metric : metrics.copy_metrics())
        REQUIRE(metric.value == 0.0f);

    for (int i = 0; i < 200; ++i)
        metrics.update(40.0f);
    metrics.update_thread();
    wait_for_average(metrics, 25.0f);

    metrics.reset_metrics();
    for (const auto& metric : metrics.copy_metrics())
        REQUIRE(metric.value == 0.0f);
    for (int i = 0; i < 200; ++i)
        metrics.update(25.0f);
    metrics.update_thread();
    wait_for_average(metrics, 40.0f);
}
