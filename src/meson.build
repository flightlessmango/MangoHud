glslang = find_program('glslangValidator')

# Needs prefix for configure_file()
if get_option('append_libdir_mangohud')
  libdir_mangohud = join_paths(get_option('prefix'), get_option('libdir'), 'mangohud')
else
  libdir_mangohud = join_paths(get_option('prefix'), get_option('libdir'))
endif

overlay_shaders = [
  'overlay.frag',
  'overlay.vert',
]
overlay_spv = []
foreach s : ['overlay.frag', 'overlay.vert']
  overlay_spv += custom_target(
    s + '.spv.h', input : s, output : s + '.spv.h',
    command : [glslang, '-V', '-x', '-o', '@OUTPUT@', '@INPUT@'])
endforeach

vklayer_files = files(
  'overlay.cpp',
  'overlay_params.cpp',
  'font_unispace.c',
  'blacklist.cpp',
  'cpu.cpp',
  'loaders/loader_nvml.cpp',
  'nvml.cpp',
  'file_utils.cpp',
  'memory.cpp',
  'config.cpp',
  'iostats.cpp',
  'gpu.cpp',
  'notify.cpp',
  'elfhacks.cpp',
  'real_dlsym.cpp',
)

opengl_files = files(
  'gl/glad.c',
  'gl/imgui_impl_opengl3.cpp',
  'gl/imgui_hud.cpp',
  'gl/inject_egl.cpp',
)

if get_option('with_dlsym').enabled()
    pre_args += '-DHOOK_DLSYM'
endif

if get_option('with_xnvctrl').enabled()

  if not get_option('with_x11').enabled()
    error('XNVCtrl also needs \'with_x11\'')
  endif

  xnvctrl_h_found = cc.has_header('NVCtrl/NVCtrl.h')
  if not xnvctrl_h_found
    error('NVCtrl.h was not found. Disable with \'with_xnvctrl\' if this feature is not needed.')
  endif

  pre_args += '-DHAVE_XNVCTRL'
  vklayer_files += files(
    'loaders/loader_nvctrl.cpp',
    'nvctrl.cpp',
  )
endif

if get_option('with_x11').enabled()
  pre_args += '-DHAVE_X11'

  vklayer_files += files(
    'loaders/loader_x11.cpp',
    'shared_x11.cpp',
  )

  opengl_files += files(
    'loaders/loader_glx.cpp',
    'gl/inject_glx.cpp',
  )
endif

if dbus_dep.found() and get_option('with_dbus').enabled()
  pre_args += '-DHAVE_DBUS'
  vklayer_files += files(
    'dbus.cpp',
    'loaders/loader_dbus.cpp',
  )
endif

vklayer_mesa_overlay = shared_library(
  'MangoHud',
  mangohud_version,
  util_files,
  vk_enum_to_str,
  vklayer_files,
  opengl_files,
  overlay_spv,
  vk_enum_to_str,
  c_args : [
    pre_args,
    c_vis_args, 
    no_override_init_args, 
    vulkan_wsi_args
    ],
  cpp_args : [
    pre_args,
    cpp_vis_args, 
    cpp_nvml_args,
    vulkan_wsi_args
    ],
  dependencies : [
    vulkan_wsi_deps, 
    libimgui_core_dep,
    dbus_dep,
    dep_dl,
    dep_pthread,
    dep_vulkan],
  include_directories : [inc_common],
  link_args : cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro', '-Wl,--exclude-libs,ALL']),
  install_dir : libdir_mangohud,
  install : true
)

mangohud_dlsym = shared_library(
  'MangoHud_dlsym',
  files(
    'elfhacks.cpp',
    'real_dlsym.cpp',
    'hook_dlsym.cpp',
  ),
  c_args : [
    pre_args,
    c_vis_args,
    no_override_init_args,
    ],
  cpp_args : [
    pre_args,
    cpp_vis_args,
    ],
  dependencies : [dep_dl],
  include_directories : [inc_common],
  link_args : cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro', '-Wl,--exclude-libs,ALL']),
  install_dir : libdir_mangohud,
  install : true
)

configure_file(input : 'mangohud.json.in',
  output : '@0@.@1@.json'.format(meson.project_name(), target_machine.cpu_family()),
  configuration : {'libdir_mangohud' : libdir_mangohud + '/',
                  'PROJECT_NAME' : meson.project_name() + ' ',
                  'LAYER_SUFFIX' : target_machine.cpu_family()},
  install : true,
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'implicit_layer.d'),
)

if target_machine.cpu_family() == 'x86_64'
  mangohud_cpu_family = ''
else
  mangohud_cpu_family = '.@0@'.format(target_machine.cpu_family())
endif

configure_file(input : '../bin/mangohud.in',
  output : 'mangohud@0@'.format(mangohud_cpu_family),
  configuration : {'libdir_mangohud' : libdir_mangohud},
  install_dir : get_option('bindir'),
)

if get_option('include_doc')
  install_data(
    files('../bin/MangoHud.conf'),
    install_dir : join_paths(get_option('datadir'), 'doc', 'mangohud'),
    rename : ['MangoHud.conf.example']
  )
endif
