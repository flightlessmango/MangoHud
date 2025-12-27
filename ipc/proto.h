#pragma once
#include <gbm.h>
#include <vector>

struct GbmBuffer {
  gbm_device* dev = nullptr;
  gbm_bo* bo = nullptr;

  int fd = -1;
  uint64_t modifier = 0;
  uint32_t stride = 0;
  uint32_t offset = 0;

  uint32_t fourcc = 0;
  uint64_t plane_size = 0;

  uint32_t width;
  uint32_t height;
};

GbmBuffer create_gbm_buffer_with_modifiers(
  int drm_fd,
  uint32_t width,
  uint32_t height,
  uint32_t drm_fourcc,
  const std::vector<uint64_t>& modifiers);

void destroy_gbm_buffer(GbmBuffer& b);
