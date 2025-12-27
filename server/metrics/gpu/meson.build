libdrm_dep = dependency('libdrm')
libcap_dep = dependency('libcap')

gpu_static = static_library(
    'gpu',
    files('gpu.cpp',
          'amdgpu/amdgpu.cpp',
          'amdgpu/gpu_metrics.cpp',
          'intel/i915/i915.cpp',
          'intel/i915/i915_drm.cpp',
          'intel/xe/xe.cpp',
          'intel/xe/xe_drm.cpp',
          'msm/dpu.cpp',
          'msm/kgsl.cpp',
          'nvidia/nvidia.cpp',
          'nvidia/nvml_loader.cpp'),
    include_directories: '../',
    dependencies: [libdrm_dep,
                   libcap_dep,
                   spdlog_dep]
)
