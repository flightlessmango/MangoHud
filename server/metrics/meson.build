subdir('cpu')
subdir('gpu')

metrics_static = static_library(
    'metrics',
    files('fdinfo.cpp',
          'hwmon.cpp',
          'memory.cpp',
          'panfrost.cpp',
          'metrics.cpp'),
    dependencies: [spdlog_dep,
                  vkbootstrap_dep,
                  dearimgui_dep],
    link_with: [cpu_static,
                gpu_static],
    include_directories: utils_inc
)
