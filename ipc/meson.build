systemd_dep = dependency('libsystemd')
gbm_dep = dependency('gbm')

ipc_inc = include_directories('.')

ipc_static = static_library(
  'ipc',
  files('ipc.cpp', 'gbm.cpp'),
  dependencies: [systemd_dep,
                 gbm_dep,
                 spdlog_dep,
                 dearimgui_dep,
                 render_dep,
                 vkbootstrap_dep],
  include_directories: [inc_common, utils_inc],
)

ipc_dep = declare_dependency(
  link_with: ipc_static,
  include_directories: [ipc_inc],
)
