#include "ipc.h"

#include <gbm.h>
#include <unistd.h>

#include <cstdlib>

GbmBuffer create_gbm_buffer_with_modifiers(
  int drm_fd,
  uint32_t w,
  uint32_t h,
  uint32_t drm_fourcc,
  const std::vector<uint64_t>& modifiers)
{
  GbmBuffer out{};
  out.fourcc = drm_fourcc;
  out.width  = w;
  out.height = h;

  out.dev = gbm_create_device(drm_fd);
  if (!out.dev) std::abort();

  // Prefer the passed modifier list if any.
  if (!modifiers.empty()) {
    out.bo = gbm_bo_create_with_modifiers2(
      out.dev,
      w,
      h,
      drm_fourcc,
      modifiers.data(),
      modifiers.size(),
      GBM_BO_USE_RENDERING
    );
  }

  if (!out.bo) {
    const uint64_t linear = DRM_FORMAT_MOD_LINEAR;
    out.bo = gbm_bo_create_with_modifiers2(out.dev, w, h, drm_fourcc,
             &linear, 1, GBM_BO_USE_RENDERING
    );
  }

  if (!out.bo) {
    out.bo = gbm_bo_create(out.dev, w, h, drm_fourcc, GBM_BO_USE_RENDERING);
  }

  if (!out.bo) std::abort();

  out.fd = gbm_bo_get_fd(out.bo);
  if (out.fd < 0) std::abort();

  out.modifier = gbm_bo_get_modifier(out.bo);
  out.stride   = gbm_bo_get_stride_for_plane(out.bo, 0);
  out.offset   = gbm_bo_get_offset(out.bo, 0);
  out.plane_size = (uint64_t)out.stride * (uint64_t)h;

  return out;
}

void destroy_gbm_buffer(GbmBuffer& b) {
  if (b.fd >= 0) close(b.fd);
  if (b.bo) gbm_bo_destroy(b.bo);
  if (b.dev) gbm_device_destroy(b.dev);
  b = {};
}
