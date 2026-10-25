#include "../src/base_fps_meter.h"
#include "../src/frame_generation_detector.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#define REQUIRE(expr) do { if (!(expr)) { \
   std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
   std::abort(); \
} } while (0)

int main()
{
   BaseFpsMeter meter;
   constexpr uint64_t step = 16666667;
   constexpr uint64_t start = 1000000000;
   for (uint64_t i = 1; i <= 30; ++i) {
      const uint64_t now = start + i * step;
      meter.record_reflex(VK_LATENCY_MARKER_RENDERSUBMIT_START_NV, i, now);
      meter.record_reflex(VK_LATENCY_MARKER_RENDERSUBMIT_START_NV, i, now);
      meter.record_reflex(VK_LATENCY_MARKER_PRESENT_START_NV, i, now);
   }
   REQUIRE(meter.fps(start + 30 * step));
   REQUIRE(std::abs(meter.fps(start + 30 * step).value_or(0.0) - 60.0) < 0.01);
   REQUIRE(!meter.fps(start + 30 * step + 1000000001));

   BaseFpsMeter sparse;
   sparse.record_reflex(VK_LATENCY_MARKER_SIMULATION_START_NV, 1, start + step);
   REQUIRE(!sparse.fps(start + step));
   for (uint64_t i = 2; i <= 5; ++i)
      sparse.record_reflex(VK_LATENCY_MARKER_SIMULATION_START_NV, i, start + i * step);
   REQUIRE(sparse.fps(start + 5 * step));
   REQUIRE(std::abs(sparse.fps(start + 5 * step).value_or(0.0) - 60.0) < 0.01);

   BaseFpsMeter amd;
   for (uint64_t i = 1; i <= 20; ++i)
      amd.record_amd(i, start + i * step);
   REQUIRE(std::abs(amd.fps(start + 20 * step).value_or(0.0) - 60.0) < 0.01);

   BaseFpsMeter boundary;
   for (uint64_t i = 1; i <= 20; ++i) {
      boundary.record_frame_boundary(i, start + i * step);
      boundary.record_frame_boundary(i, start + i * step);
   }
   REQUIRE(std::abs(boundary.fps(start + 20 * step).value_or(0.0) - 60.0) < 0.01);

   FrameGenerationDetector detector;
   for (uint64_t t = start; t < start + 700000000; t += 100000000)
      REQUIRE(!detector.observe(t, 96.0, 48.0));
   REQUIRE(detector.observe(start + 700000000, 96.0, 48.0));
   REQUIRE(detector.observe(start + 800000000, 52.0, 48.0));
   for (uint64_t t = start + 900000000; t < start + 1500000000; t += 100000000)
      REQUIRE(detector.observe(t, 48.0, 48.0));
   REQUIRE(!detector.observe(start + 1500000000, 48.0, 48.0));
   for (uint64_t t = start + 1600000000; t < start + 2300000000; t += 100000000)
      REQUIRE(!detector.observe(t, 96.0, 48.0));
   REQUIRE(detector.observe(start + 2300000000, 96.0, 48.0));

   BaseFpsMeter stopped_markers;
   for (uint64_t i = 1; i <= 20; ++i)
      stopped_markers.record_reflex(VK_LATENCY_MARKER_SIMULATION_START_NV,
                                   i, start + i * step);
   const uint64_t last_marker_ns = start + 20 * step;
   FrameGenerationDetector marker_loss;
   for (uint64_t offset = 0; offset < 700000000; offset += 100000000)
      REQUIRE(!marker_loss.observe(last_marker_ns + offset, 120.0,
                                  stopped_markers.fps(last_marker_ns + offset).value_or(0.0)));
   REQUIRE(marker_loss.observe(last_marker_ns + 700000000, 120.0,
                               stopped_markers.fps(last_marker_ns + 700000000).value_or(0.0)));
   REQUIRE(!stopped_markers.fps(last_marker_ns + 1100000000));
   for (uint64_t offset = 1100000000; offset < 1800000000; offset += 100000000)
      REQUIRE(marker_loss.observe(last_marker_ns + offset, 120.0,
                                  stopped_markers.fps(last_marker_ns + offset).value_or(0.0)));
   REQUIRE(!marker_loss.observe(last_marker_ns + 1800000000, 120.0, 0.0));

   FrameGenerationDetector no_marker;
   for (uint64_t t = start; t < start + 2000000000; t += 100000000)
      REQUIRE(!no_marker.observe(t, 100.0, 0.0));
}
