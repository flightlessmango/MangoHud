subdir('cpu')
subdir('gpu')

metrics_lib = static_library(
    'metrics',
    files('fdinfo.cpp',
          'hwmon.cpp',
          'memory.cpp',
          'panfrost.cpp',
          'metrics.cpp'),
    dependencies: [spdlog_dep,
                  vkbootstrap_dep,
                  dearimgui_dep,
                  implot_dep,
                  vulkan_dep],
    link_with: [cpu_lib,
                gpu_lib],
    include_directories: utils_inc
)
