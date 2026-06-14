utils_inc = include_directories('.')

utils_lib = static_library(
    'utils',
    files('file_utils.cpp',
          'mesa/os_socket.c',
          'mesa/os_time.c'),
    include_directories: inc_common,
    dependencies: spdlog_dep,
    install: false
)

utils_dep = declare_dependency(
    include_directories: utils_inc,
    link_with: utils_lib
)
