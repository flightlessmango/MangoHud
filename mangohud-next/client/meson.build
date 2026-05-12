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

vulkan_layer = shared_library(
    'MangoHud-next',
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
                   dependency('egl'),
                   dependency('gl'),
                   vkbootstrap_dep,
                   dep_vulkan_utility_libraries],
    include_directories: inc_common,
    install: true,
    link_args: ['-static-libstdc++'],
)

conf_data = configuration_data()

if get_option('dynamic_string_tokens')
  next_ld_libdir_mangohud = get_option('prefix') + '/lib/mangohud/\$LIB/'
else
  next_ld_libdir_mangohud = libdir_mangohud + '/'
endif

conf_data.set('ld_libdir_mangohud_abs', libdir_mangohud)
conf_data.set('ld_libdir_mangohud', next_ld_libdir_mangohud)
conf_data.set('mangohud_lib_name', 'libMangoHud-next.so')
conf_data.set('mangohud_env', 'MANGOHUD_NEXT')
conf_data.set('cpu_family', host_machine.cpu_family())
conf_data.set('version', '@VCS_TAG@')

configure_file(input : 'mangohud.json.in',
  output : '@0@-next.@1@.json'.format(meson.project_name(), host_machine.cpu_family()),
  configuration : conf_data,
  install : true,
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'implicit_layer.d'),
  install_tag : 'runtime',
)

configured_mangohud_next = configure_file(input : '../../bin/mangohud.in',
  output : 'mangohud-next.vcs.in',
  configuration : conf_data,
)

vcs_tag(input : configured_mangohud_next,
  output : 'mangohud-next',
  command : vcs_tag_command,
  fallback : vcs_tag_fallback,
  install : true,
  install_dir : get_option('bindir'),
  install_tag : 'scripts',
  install_mode : 'rwxr-xr-x',
)
