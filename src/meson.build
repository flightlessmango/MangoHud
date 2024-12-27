glslang = find_program('glslang', 'glslangValidator')

if get_option('dynamic_string_tokens')
  ld_prefix = get_option('prefix') + '/\$LIB/'
else
  ld_prefix = join_paths(get_option('prefix') ,get_option('libdir')) + '/'
endif

# Needs prefix for configure_file()
if get_option('append_libdir_mangohud')
  libdir_mangohud = join_paths(get_option('prefix'), get_option('libdir'), 'mangohud')
  ld_libdir_mangohud = ld_prefix + 'mangohud/'
else
  libdir_mangohud = join_paths(get_option('prefix'), get_option('libdir'))
  ld_libdir_mangohud = ld_prefix
endif

git = find_program('git', required: false)
if git.found()
  git_describe = run_command([git, 'describe', '--tags', '--dirty=+'], check: false)
endif
if git.found() and git_describe.returncode() == 0
  describe_ver = git_describe.stdout().strip()
else
  describe_ver = meson.project_version()
endif

conf_data = configuration_data()

conf_data.set('ld_libdir_mangohud_abs', libdir_mangohud)
conf_data.set('ld_libdir_mangohud', ld_libdir_mangohud)
conf_data.set('cpu_family', host_machine.cpu_family())
conf_data.set('version', describe_ver)

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

util_files = files(
  'mesa/util/os_socket.c',
  'mesa/util/os_time.c',
)

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
  'blacklist.cpp',
  'file_utils.cpp',
  'nvidia.cpp',
  'gpu_fdinfo.cpp',
  'amdgpu.cpp'
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
    'memory.cpp',
    'iostats.cpp',
    'notify.cpp',
    'elfhacks.c',
    'real_dlsym.c',
    'pci_ids.cpp',
    'battery.cpp',
    'control.cpp',
    'device.cpp',
    'net.cpp',
    'shell.cpp',
    'ftrace.cpp',
  )

  if get_option('with_fex')
    pre_args += '-DHAVE_FEX'
    vklayer_files += files(
      'fex.cpp',
    )
  endif

  opengl_files = files(
    'gl/glad.c',
    'gl/gl_renderer.cpp',
    'gl/gl_hud.cpp',
    'gl/inject_egl.cpp',
  )

  nvml_h_found = get_option('with_nvml') == 'enabled'
  if get_option('with_nvml') == 'system'
    nvml_h_found = cc.has_header('nvml.h')
    if not nvml_h_found
      error('nvml.h was not found. Disable with \'-Dwith_nvml=disabled\' if gpu stats by NVML are not needed.')
    endif
    pre_args += '-DUSE_SYSTEM_NVML'
  endif

  if nvml_h_found
    pre_args += '-DHAVE_NVML'
    pre_args += '-DNVML_NO_UNVERSIONED_FUNC_DEFS'
    vklayer_files += files(
      'loaders/loader_nvml.cpp',
    )
  endif

  if get_option('with_xnvctrl').enabled()

    if not get_option('with_x11').enabled()
      error('XNVCtrl also needs \'with_x11\'')
    endif

    xnvctrl_h_found = cc.has_header('NVCtrl/NVCtrl.h')
    if not xnvctrl_h_found
      error('NVCtrl.h was not found. Disable with \'-Dwith_xnvctrl=disabled\' if gpu stats by XNVCtrl are not needed.')
    endif

    pre_args += '-DHAVE_XNVCTRL'
    vklayer_files += files(
      'loaders/loader_nvctrl.cpp'
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

  if get_option('with_wayland').enabled()
    pre_args += '-DHAVE_WAYLAND'

    vklayer_files += files(
      'wayland_keybinds.cpp'
    )
  endif

  if dbus_dep.found() and get_option('with_dbus').enabled()
    pre_args += '-DHAVE_DBUS'
    vklayer_files += files(
      'dbus.cpp',
      'loaders/loader_dbus.cpp',
    )
  endif
endif

link_args = cpp.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro', '-Wl,--exclude-libs,ALL', '-lGL'])

# Approach for libc++ detection taken from https://github.com/mesonbuild/meson/commit/675b47b0692131fae974298829ba807d730ab098
# Rationale for the approach is explained in this SO answer https://stackoverflow.com/a/31658120
if cpp.has_header('<version>')
  header = 'version'
else
  header = 'ciso646'
endif
is_libcxx = cpp.has_header_symbol(header, '_LIBCPP_VERSION')

# https://github.com/llvm/llvm-project/issues/38622
if is_libcxx
  message('Detected libcxx, not appending -static-libstc++ to linker arguments')
else
  link_args+=['-static-libstdc++']
endif

mangohud_static_lib = static_library(
  'MangoHud',
  mangohud_version,
  files('vulkan.cpp'),
  util_files,
  vk_enum_to_str,
  vklayer_files,
  overlay_spv,
  c_args : [
    pre_args,
    vulkan_wsi_args
    ],
  cpp_args : [
    pre_args,
    vulkan_wsi_args
    ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    mangohud_version_dep,
    vulkan_wsi_deps,
    dearimgui_dep,
    spdlog_dep,
    dbus_dep,
    dep_dl,
    dep_rt,
    dep_pthread,
    dep_vulkan,
    windows_deps,
    implot_dep],
  include_directories : [inc_common],
  link_args : link_args,
  install_dir : libdir_mangohud,
  install : false
)

mangohud_shared_lib = shared_library(
  'MangoHud',
  objects: mangohud_static_lib.extract_all_objects(),
  link_with: mangohud_static_lib,
  link_args : [link_args, '-Wl,--version-script,@0@'.format(
    join_paths(meson.current_source_dir(), 'mangohud.version'))],
  install_dir : libdir_mangohud,
  install: true
)

if is_unixy
  mangohud_opengl_versionscript = cpp.preprocess(
    'mangohud_opengl.version',
    compile_args: pre_args,
  )

  mangohud_opengl_shared_lib = shared_library(
    'MangoHud_opengl',
    mangohud_version,
    opengl_files,
    vklayer_files,
    util_files,
    c_args : [
      pre_args,
      vulkan_wsi_args
      ],
    cpp_args : [
      pre_args,
      vulkan_wsi_args
      ],
    dependencies : [
      mangohud_version_dep,
      vulkan_wsi_deps,
      dearimgui_dep,
      spdlog_dep,
      dbus_dep,
      dep_dl,
      dep_rt,
      dep_pthread,
      dep_vulkan,
      windows_deps,
      implot_dep],
    include_directories : [inc_common],
    link_args : [link_args, '-Wl,--version-script,@0@'.format(mangohud_opengl_versionscript[0].full_path())],
    link_with: mangohud_static_lib,
    link_depends: mangohud_opengl_versionscript,
    install_dir : libdir_mangohud,
    install: true
  )
endif

if get_option('mangoapp')
  if not get_option('with_x11').enabled()
    error('mangoapp also needs \'with_x11\'')
  endif
  pre_args += '-DMANGOAPP'
  mangoapp = executable(
    'mangoapp',
    files(
      'app/main.cpp',
    ),
    c_args : [
      pre_args,
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
      dep_vulkan,
      spdlog_dep,
      dbus_dep,
      dep_x11,
      dep_wayland_client,
      glfw3_dep,
      implot_dep
    ],
    include_directories : [inc_common],
    install_tag : 'mangoapp',
    link_with: mangohud_static_lib,
    link_args : link_args,
    install : true
  )
endif

if get_option('mangohudctl')
mangoapp = executable(
  'mangohudctl',
  files('app/control.c'),
  install_tag : 'mangoapp', #TODO MangoHud layer itself currently doesn't support it
  install : true
)
endif
if is_unixy
  mangohud_shim = shared_library(
    'MangoHud_shim',
    files(
      'gl/shim.c',
      'real_dlsym.c',
      'elfhacks.c'
    ),
    dependencies : [
      dep_dl
    ],
    c_args : [
      pre_args
    ],
    cpp_args : [
      pre_args
    ],
    include_directories : [inc_common],
    install_dir : libdir_mangohud,
    install: true
  )
endif

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
