mangohud_legacy_gpu_wrapper = static_library(
  'MangoHud_LegacyGPUWrapper',
  files(
    'wrapper.cpp',
    '../server/common/helpers.cpp',
    '../server/metrics/fdinfo.cpp',
    '../server/metrics/hwmon.cpp',
    '../server/metrics/gpu/gpu.cpp',
    '../server/metrics/gpu/intel/i915/i915.cpp',
    '../server/metrics/gpu/intel/i915/i915_drm.cpp',
    '../server/metrics/gpu/intel/xe/xe.cpp',
    '../server/metrics/gpu/intel/xe/xe_drm.cpp',
    '../server/metrics/gpu/amdgpu/amdgpu.cpp',
    '../server/metrics/gpu/amdgpu/gpu_metrics.cpp',
    '../server/metrics/gpu/nvidia/nvidia.cpp',
    '../server/metrics/gpu/nvidia/nvml_loader.cpp',
    '../server/metrics/gpu/msm/dpu.cpp',
    '../server/metrics/gpu/msm/kgsl.cpp',
    '../server/metrics/gpu/panfrost.cpp',
    '../server/metrics/gpu/panthor.cpp'
  ),
  cpp_args: '-DMANGOHUD_LEGACY -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE',
  include_directories: [
    '../server/metrics',
    '../server/metrics/gpu'
  ],
  dependencies: [spdlog_dep, libcap_dep, libdrm_dep]
)
