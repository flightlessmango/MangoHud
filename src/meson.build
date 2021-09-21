glslang = find_program('glslangValidator')

# Needs prefix for configure_file()
if get_option('append_libdir_mangohud')
  libdir_mangohud = join_paths(get_option('libdir'), 'mangohud')
  ld_libdir_mangohud = get_option('prefix') + '/\$LIB/mangohud/'
else
  libdir_mangohud = get_option('libdir')
  ld_libdir_mangohud = get_option('prefix') + '/\$LIB/'
endif

# For build.sh
if get_option('ld_libdir_prefix')
  # FIXME derive from libdir
  ld_libdir_mangohud = get_option('prefix') + '/lib/mangohud/\$LIB/'
endif

conf_data = configuration_data()

if get_option('ld_libdir_abs')
  conf_data.set('ld_libdir_mangohud_abs', ld_libdir_mangohud)
  conf_data.set('ld_libdir_mangohud', '')
else
  conf_data.set('ld_libdir_mangohud_abs', '')
  conf_data.set('ld_libdir_mangohud', ld_libdir_mangohud)
endif

ld_libdir_mangohud_vk = ''
if get_option('prepend_libdir_vk')
  ld_libdir_mangohud_vk = ld_libdir_mangohud
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
  'hud_elements.cpp',
  'overlay.cpp',
  'overlay_params.cpp',
  'font.cpp',
  'keybinds.cpp',
  'font_unispace.c',
  'logging.cpp',
  'config.cpp',
  'gpu.cpp',
  'vulkan.cpp',
  'blacklist.cpp',
)
opengl_files  = []
if ['windows', 'mingw'].contains(host_machine.system())
  vklayer_files += files(
    'file_utils_win32.cpp',
    'cpu_win32.cpp',
    'nvapi.cpp',
    'win/dxgi.cpp',
    'win/main.cpp',
    'win/kiero.cpp',
    'win/d3d12_hook.cpp',
    'win/d3d11_hook.cpp',
    'win/d3d_shared.cpp',
  )
endif

if is_unixy
  vklayer_files += files(
    'cpu.cpp',
    'file_utils.cpp',
    'memory.cpp',
    'iostats.cpp',
    'notify.cpp',
    'elfhacks.cpp',
    'real_dlsym.cpp',
    'pci_ids.cpp',
    'battery.cpp',
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

  nvml_h_found = get_option('with_nvml') == 'enabled'
  if get_option('with_nvml') == 'system'
    nvml_h_found = cc.has_header('nvml.h')
    if not nvml_h_found
      error('nvml.h was not found. Disable with \'-Dwith_nvml=disabled\' if gpu stats by NVML is not needed.')
    endif
    pre_args += '-DUSE_SYSTEM_NVML'
  endif

  if nvml_h_found
    pre_args += '-DHAVE_NVML'
    vklayer_files += files(
      'nvml.cpp',
      'loaders/loader_nvml.cpp',
    )
  endif

  if get_option('with_xnvctrl').enabled()

    if not get_option('with_x11').enabled()
      error('XNVCtrl also needs \'with_x11\'')
    endif

    xnvctrl_h_found = cc.has_header('NVCtrl/NVCtrl.h')
    if not xnvctrl_h_found
      error('NVCtrl.h was not found. Disable with \'-Dwith_xnvctrl=disabled\' if gpu stats by XNVCtrl is not needed.')
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

#  if get_option('with_libdrm_amdgpu').enabled() and dep_libdrm.found() and dep_libdrm_amdgpu.found()
  if get_option('with_libdrm_amdgpu').enabled() and dep_libdrm.found()
    pre_args += '-DHAVE_LIBDRM_AMDGPU'
    #if dep_xcb.found() and dep_xcb_dri2.found()
      vklayer_files += files(
        'loaders/loader_libdrm.cpp',
      )
    #endif
  endif
endif

link_args = cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro', '-Wl,--exclude-libs,ALL', '-lglfw', '-lGL'])
# meson fails to check version-script so just force add
link_args += '-Wl,--version-script,@0@'.format(join_paths(meson.current_source_dir(), 'mangohud.version'))

vklayer_mesa_overlay = shared_library(
  'MangoHud',
  mangohud_version,
  util_files,
  vk_enum_to_str,
  vklayer_files,
  opengl_files,
  overlay_spv,
  c_args : [
    pre_args,
    no_override_init_args,
    vulkan_wsi_args
    ],
  cpp_args : [
    pre_args,
    vulkan_wsi_args
    ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    vulkan_wsi_deps,
    dearimgui_dep,
    spdlog_dep,
    dep_libdrm,
    #dep_libdrm_amdgpu,
    dbus_dep,
    dep_dl,
    dep_rt,
    dep_pthread,
    dep_vulkan,
    windows_deps],
  include_directories : [inc_common],
  link_args : link_args,
  install_dir : libdir_mangohud,
  install : true
)
if is_unixy
  mangohud_dlsym = shared_library(
    'MangoHud_dlsym',
    files(
      'elfhacks.cpp',
      'real_dlsym.cpp',
      'hook_dlsym.cpp',
    ),
    c_args : [
      pre_args,
      no_override_init_args,
      ],
    cpp_args : [
      pre_args,
      ],
    gnu_symbol_visibility : 'hidden',
    dependencies : [dep_dl],
    include_directories : [inc_common],
    link_args : link_args,
    install_dir : libdir_mangohud,
    install : true
  )
endif

if sizeof_ptr == 8
  pre_args += '-DIMGUI_IMPL_OPENGL_LOADER_GL3W'
  overlay_files = files(
    'overlay.cpp',
    'hud_elements.cpp',
    'gpu.cpp',
    'nvml.cpp',
    'loaders/loader_nvml.cpp',
    'loaders/loader_nvctrl.cpp',
    'nvctrl.cpp',
    'logging.cpp',
    'battery.cpp',
    'memory.cpp',
    'cpu.cpp',
    'pci_ids.cpp',
    'file_utils.cpp',
    'iostats.cpp',
    'dbus.cpp',
    'loaders/loader_libdrm.cpp',
    'overlay_params.cpp',
    'loaders/loader_x11.cpp',
    'loaders/loader_dbus.cpp',
    'config.cpp',
    'blacklist.cpp',
    'font.cpp',
    'keybinds.cpp',
    'font_unispace.c',
    'loaders/loader_x11.cpp',
    'shared_x11.cpp',
  )
  mangoapp = executable(
    'mangoapp',
    mangohud_version,
    util_files,
    vk_enum_to_str,
    overlay_files,
    files(
      '../subprojects/imgui-1.81/backends/imgui_impl_opengl3.cpp',
      '../subprojects/imgui-1.81/backends/imgui_impl_glfw.cpp',
      '../include/GL/gl3w.c',
      'app/main.cpp',
    ),
    c_args : [
      pre_args,
      no_override_init_args,
      vulkan_wsi_args
      ],
    cpp_args : [
      pre_args,
      vulkan_wsi_args
      ],
    gnu_symbol_visibility : 'hidden',
    dependencies : [
      dearimgui_dep,
      dep_dl,
      spdlog_dep,
      dbus_dep,
      dep_libdrm,
      dep_x11,
    ],
    include_directories : [inc_common],
    link_args : link_args,
    install : true
  )
endif

configure_file(input : 'mangohud.json.in',
  output : '@0@.json'.format(meson.project_name()),
  configuration : {'ld_libdir_mangohud' : ld_libdir_mangohud_vk,
                  'PROJECT_NAME' : meson.project_name().to_upper()},
  install : true,
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'implicit_layer.d'),
)

configure_file(input : '../bin/mangohud.in',
  output : 'mangohud',
  configuration : conf_data,
  install_dir : get_option('bindir'),
)

if get_option('include_doc')
  install_data(
    files('../bin/MangoHud.conf'),
    install_dir : join_paths(get_option('datadir'), 'doc', 'mangohud'),
    rename : ['MangoHud.conf.example']
  )
endif
