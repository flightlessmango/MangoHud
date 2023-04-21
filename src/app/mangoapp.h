#pragma once
#include <stdint.h>
#include <mutex>
#include <condition_variable>
#include <vector>

extern std::mutex mangoapp_m;
extern std::condition_variable mangoapp_cv;
