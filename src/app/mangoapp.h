#include <stdint.h>
#include <mutex>
#include <condition_variable>
#include <vector>

extern std::mutex mangoapp_m;
extern std::condition_variable mangoapp_cv;

extern uint8_t g_fsrUpscale;
extern uint8_t g_fsrSharpness;
extern std::vector<float> gamescope_debug_latency;
extern std::vector<float> gamescope_debug_app;
