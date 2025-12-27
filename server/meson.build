subdir('metrics')

# yaml_dep_sp = subproject('yaml-cpp', default_options: [
#   'tests=disabled',
#   'tools=false',
# ])
# yaml_dep = yaml_dep_sp.get_variable('yaml_cpp_dep')
yaml_dep = dependency('yaml-cpp')
egl_dep = dependency('egl')
glesv2_dep = dependency('glesv2')

server_static = static_library(
    'server',
        files('config.cpp',
          'common/helpers.cpp',
          'egl_window.cpp'),
        dependencies: [yaml_dep,
                      spdlog_dep,
                      glfw3_dep,
                      vkbootstrap_dep,
                      glesv2_dep,
                      egl_dep,
                      dearimgui_dep,
                      utils_dep,
                      ipc_dep,
                      render_dep],
        link_with: [metrics_static],
        include_directories: inc_common
)

server = executable(
    'MangoHudServer',
    files('main.cpp'),
    dependencies: [spdlog_dep,
                   vulkan_dep,
                   vkbootstrap_dep,
                   libdrm_dep,
                   dearimgui_dep,
                   ipc_dep,
                   render_dep],
    link_with: [server_static, ipc_static, font_static, static_render],
    include_directories: inc_common
)

# cmocka_dep = dependency('cmocka')

# e = executable('config_test',
#                    files('../tests/server/config.cpp'),
#                     link_with: server_static_lib,
#                     include_directories: '.',
#                     dependencies: cmocka_dep
#                   )

# test('config', e, workdir: meson.project_source_root())
