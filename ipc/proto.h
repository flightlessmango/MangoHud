#pragma once
#include <gbm.h>
#include <vector>
#include <vulkan/vulkan.h>

struct Sample {
    uint64_t seq;
    uint64_t t_ns;
};

constexpr size_t   FT_MAX = 200;
constexpr uint64_t KEEP_NS = 500000000ULL;

