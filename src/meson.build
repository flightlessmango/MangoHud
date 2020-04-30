# Copyright © 2019 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

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
  'config.cpp',
  'nvml.cpp',
  'gpu.cpp',
)

opengl_files = []

if ['windows', 'mingw'].contains(host_machine.system())
  vklayer_files += files(
    'kiero/main.cpp',
    'kiero/kiero.cpp',
    'kiero/dx_shared.cpp',
    'kiero/d3d11_impl.cpp',
    'kiero/d3d9_impl.cpp',
    'kiero/imgui_impl_dx11.cpp',
    'kiero/imgui_impl_dx9.cpp',
    'kiero/imgui_impl_win32.cpp',
    'file_utils_win32.cpp',
    'loaders/loader_nvml_win32.cpp',
    'cpu_win32.cpp',
#    'MangoHud.def',
  )
endif

if is_unixy
  vklayer_files += files(
    'file_utils.cpp',
    'cpu.cpp',
    'memory.cpp',
    'iostats.cpp',
    'notify.cpp',
    'elfhacks.cpp',
    'real_dlsym.cpp',
    'loaders/loader_nvml.cpp',
  )

  pre_args += '-DHAVE_NVML'
  opengl_files += files(
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
endif

vklayer_mesa_overlay = shared_library(
  'MangoHud',
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
    dep_vulkan,
    windows_deps],
  include_directories : [inc_common],
  link_args : cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro', '-Wl,--exclude-libs,ALL']),
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
endif

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
