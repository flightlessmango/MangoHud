glslang = find_program('glslang', 'glslangValidator')

overlay_shaders = [
  'overlay.frag',
  'overlay.vert'
]
overlay_spv = []
foreach s : ['overlay.frag', 'overlay.vert']
  overlay_spv += custom_target(
    s + '.spv.h', input : s, output : s + '.spv.h',
    command : [glslang, '-V', '-x', '-o', '@OUTPUT@', '@INPUT@'])
endforeach

vkroots_dep = subproject('vkroots').get_variable('vkroots_dep')
gl_dep = dependency('gl')
egl_dep = dependency('egl')
vulkan_layer = shared_library(
    'MangoHud',
    files('layer.cpp',
          'vulkan.cpp',
          'gl.cpp',
          'gl_hooks.cpp',
          'real_dlsym.c',
          'elfhacks.c'),
    overlay_spv,
    dependencies: [vulkan_dep,
                   vulkan_wsi_deps,
                   spdlog_dep,
                   vkroots_dep,
                   ipc_dep,
                   dearimgui_dep,
                   egl_dep,
                   utils_dep,
                   ipc_dep,
                   gl_dep,
                   vkbootstrap_dep],
    include_directories: inc_common,
    install: true,
    link_args: ['-static-libstdc++'],
    cpp_args: ['-DVK_LOADER_LAYER_INTERFACE_VERSION=2']
)

conf_data = configuration_data()

conf_data.set('ld_libdir_mangohud_abs', libdir_mangohud)
conf_data.set('ld_libdir_mangohud', ld_libdir_mangohud)
conf_data.set('cpu_family', host_machine.cpu_family())
conf_data.set('version', describe_ver)

configure_file(input : 'mangohud.json.in',
  output : '@0@.@1@.json'.format(meson.project_name(), host_machine.cpu_family()),
  configuration : conf_data,
  install : true,
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'implicit_layer.d'),
  install_tag : 'runtime',
)

configure_file(input : '../bin/mangohud.in',
  output : 'mangohud',
  configuration : conf_data,
  install_dir : get_option('bindir'),
  install_tag : 'scripts',
)
