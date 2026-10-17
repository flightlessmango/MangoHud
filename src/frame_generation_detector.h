#pragma once

#include <cmath>
#include <cstdint>

// Classify frame generation only when an independent application-frame marker
// is available. The ratio alone is never used to invent a base frame rate.
class FrameGenerationDetector {
public:
   bool observe(uint64_t now_ns, double present_fps, double base_fps)
   {
      const bool valid = std::isfinite(present_fps) && std::isfinite(base_fps) &&
                         present_fps > 0.0 && base_fps > 0.0;
      const double ratio = valid ? present_fps / base_fps : 0.0;
      const bool generated = valid && ratio >= 1.4 && present_fps - base_fps >= 12.0;
      const bool native = !valid || ratio <= 1.15 || present_fps - base_fps < 6.0;

      const int state = generated ? 1 : native ? 0 : -1;
      if (state < 0) {
         candidate_since_ns = 0;
      } else if (state != candidate || !candidate_since_ns) {
         candidate = state;
         candidate_since_ns = now_ns;
      } else {
         if (candidate_since_ns && now_ns >= candidate_since_ns &&
             now_ns - candidate_since_ns >= settle_ns)
            active_ = candidate == 1;
      }
      return active_;
   }

   bool active() const { return active_; }

private:
   static constexpr uint64_t settle_ns = 700000000;
   bool active_ = false;
   int candidate = -1;
   uint64_t candidate_since_ns = 0;
};
