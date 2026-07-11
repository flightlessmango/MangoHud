glslang = find_program('glslang', 'glslangValidator')
wayland_scanner = find_program('wayland-scanner')
wayland_protocols_dep = dependency('wayland-protocols')
wayland_protocols_dir = wayland_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')
linux_dmabuf_xml = wayland_protocols_dir / 'stable/linux-dmabuf/linux-dmabuf-v1.xml'
fractional_scale_xml = wayland_protocols_dir / 'staging/fractional-scale/fractional-scale-v1.xml'
viewporter_xml = wayland_protocols_dir / 'stable/viewporter/viewporter.xml'

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

linux_dmabuf_protocol = [
  custom_target(
    'linux-dmabuf-v1-client-protocol.h',
    input: linux_dmabuf_xml,
    output: 'linux-dmabuf-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
  custom_target(
    'linux-dmabuf-v1-protocol.c',
    input: linux_dmabuf_xml,
    output: 'linux-dmabuf-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
]

fractional_scale_protocol = [
  custom_target(
    'fractional-scale-v1-client-protocol.h',
    input: fractional_scale_xml,
    output: 'fractional-scale-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
  custom_target(
    'fractional-scale-v1-protocol.c',
    input: fractional_scale_xml,
    output: 'fractional-scale-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
]

viewporter_protocol = [
  custom_target(
    'viewporter-client-protocol.h',
    input: viewporter_xml,
    output: 'viewporter-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
  custom_target(
    'viewporter-protocol.c',
    input: viewporter_xml,
    output: 'viewporter-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
]

vulkan_layer = shared_library(
    'MangoHud-next',
    files('layer.cpp',
          'vulkan.cpp',
          'wayland.cpp',
          'gl.cpp',
          'gl_hooks.cpp',
          'real_dlsym.c',
          'elfhacks.c'),
    overlay_spv,
    linux_dmabuf_protocol,
    fractional_scale_protocol,
    viewporter_protocol,
    dependencies: [vulkan_dep,
                   vulkan_wsi_deps,
                   spdlog_dep,
                   vkroots_dep,
                   ipc_dep,
                   dearimgui_dep,
                   dependency('egl'),
                   dependency('wayland-egl'),
                   dependency('gl'),
                   vkbootstrap_dep,
                   dep_vulkan_utility_libraries],
    cpp_args: vulkan_wsi_args,
    include_directories: inc_common,
    install: true,
    link_args: [
      '-static-libstdc++',
      '-Wl,--version-script,@0@'.format(join_paths(meson.current_source_dir(), 'mangohud.version')),
    ],
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
