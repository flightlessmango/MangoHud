subdir('metrics')

# yaml_dep_sp = subproject('yaml-cpp', default_options: [
#   'tests=disabled',
#   'tools=false',
# ])
# yaml_dep = yaml_dep_sp.get_variable('yaml_cpp_dep')
yaml_dep = dependency('yaml-cpp')
egl_dep = dependency('egl')

server_lib = static_library(
    'server',
        files('config.cpp',
          'common/helpers.cpp'),
        dependencies: [yaml_dep,
                      spdlog_dep,
                      vkbootstrap_dep,
                      egl_dep,
                      dearimgui_dep,
                      utils_dep,
                      ipc_dep,
                      render_dep],
        link_with: [metrics_lib],
        include_directories: inc_common
)

server = executable(
    'mangohud-server',
    files('main.cpp'),
    dependencies: [spdlog_dep,
                   vulkan_dep,
                   vkbootstrap_dep,
                   libdrm_dep,
                   dearimgui_dep,
                   implot_dep,
                   ipc_dep,
                   render_dep,
                   utils_dep,
                   ipc_dep],
    link_with: [server_lib, font_lib, metrics_lib],
    include_directories: inc_common,
    install: true
)

# cmocka_dep = dependency('cmocka')

# e = executable('config_test',
#                    files('../tests/server/config.cpp'),
#                     link_with: server_static_lib,
#                     include_directories: '.',
#                     dependencies: cmocka_dep
#                   )

# test('config', e, workdir: meson.project_source_root())
