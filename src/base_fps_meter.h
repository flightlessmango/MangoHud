#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>

#include <vulkan/vulkan_core.h>

// Measures application frame cadence from explicit per-frame markers. This
// never infers a multiplier from the output FPS: without a marker, the base
// rate is unknown. The ordinary MangoHud FPS remains the Vulkan present rate.
class BaseFpsMeter {
public:
   void record_reflex(VkLatencyMarkerNV marker, uint64_t present_id, uint64_t now_ns)
   {
      unsigned index;
      switch (marker) {
      case VK_LATENCY_MARKER_PRESENT_START_NV: index = 0; break;
      case VK_LATENCY_MARKER_RENDERSUBMIT_START_NV: index = 1; break;
      case VK_LATENCY_MARKER_SIMULATION_START_NV: index = 2; break;
      default: return;
      }

      record(index, present_id, now_ns);
   }

   void record_amd(uint64_t frame_id, uint64_t now_ns)
   {
      record(3, frame_id, now_ns);
   }

   void record_frame_boundary(uint64_t frame_id, uint64_t now_ns)
   {
      record(4, frame_id, now_ns);
   }

   std::optional<double> fps(uint64_t now_ns) const
   {
      for (const auto &stream : streams) {
         if (!stream.last_ns || now_ns < stream.last_ns || now_ns - stream.last_ns > stale_ns)
            continue;
         if (stream.samples.size() < 4)
            continue;
         const auto elapsed = stream.samples.back().second - stream.samples.front().second;
         if (elapsed)
            return 1e9 * (stream.samples.size() - 1) / elapsed;
      }
      return std::nullopt;
   }

private:
   void record(unsigned index, uint64_t present_id, uint64_t now_ns)
   {
      if (!present_id)
         return;

      auto &stream = streams[index];
      if (stream.last_id == present_id)
         return;
      if (present_id < stream.last_id || (stream.last_ns && now_ns <= stream.last_ns))
         stream.samples.clear();
      stream.last_id = present_id;
      stream.last_ns = now_ns;
      stream.samples.emplace_back(present_id, now_ns);
      while (!stream.samples.empty() && now_ns - stream.samples.front().second > window_ns)
         stream.samples.pop_front();
   }

   struct Stream {
      uint64_t last_id = 0;
      uint64_t last_ns = 0;
      std::deque<std::pair<uint64_t, uint64_t>> samples;
   };
   static constexpr uint64_t window_ns = 1000000000;
   static constexpr uint64_t stale_ns = 1000000000;
   std::array<Stream, 5> streams;
};
