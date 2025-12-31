systemd_dep = dependency('libsystemd')
gbm_dep = dependency('gbm')

ipc_inc = include_directories('.')

ipc_lib = static_library(
  'ipc',
  files('ipc.cpp',
        'ipc_client.cpp'),
  dependencies: [systemd_dep,
                 spdlog_dep,
                 dearimgui_dep,
                 implot_dep,
                 render_dep,
                 vkbootstrap_dep,
                 vulkan_dep,
                 utils_dep],
  include_directories: [inc_common],
)

ipc_dep = declare_dependency(
  link_with: ipc_lib,
  include_directories: [ipc_inc],
)
