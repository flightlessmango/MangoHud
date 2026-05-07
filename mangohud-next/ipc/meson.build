systemd_dep = dependency('libsystemd')

ipc_inc = include_directories('.')

ipc_abi_inputs = files(
  'client.h',
  'ipc.h',
  'ipc_client.h',
  'client.cpp',
  'ipc.cpp',
  'ipc_client.cpp',
)

ipc_abi_hash = custom_target(
  'ipc_abi_hash',
  input: ipc_abi_inputs,
  output: 'ipc_abi_hash.h',
  command: [
    'sh', '-c',
    'hash=$(cat "$@" | sha256sum | cut -c1-16); printf "#pragma once\n\n#define MANGOHUD_IPC_ABI_HASH 0x%sULL\n" "$hash" > "$0"',
    '@OUTPUT@',
    '@INPUT@',
  ],
)

ipc_lib = static_library(
  'ipc',
  files('ipc.cpp',
        'ipc_client.cpp',
        'client.cpp'),
  ipc_abi_hash,
  dependencies: [systemd_dep,
                 spdlog_dep,
                 dearimgui_dep,
                 implot_dep,
                 vkbootstrap_dep,
                 vulkan_dep,
                 utils_dep,
                 vkroots_dep,
                 mangohud_version_dep,
                 dep_vulkan_utility_libraries],
  include_directories: [inc_common],
)

ipc_dep = declare_dependency(
  link_with: ipc_lib,
  include_directories: [ipc_inc],
)
