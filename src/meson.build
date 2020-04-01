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
  'gl/imgui_hud_shared.cpp',
)

pre_args += '-DHOOK_DLSYM'
inc_opengl = include_directories('gl/gl3w')

glimgui_glx_dep = null_dep
glimgui_egl_dep = null_dep

if get_option('with_x11').enabled() and \
    get_option('with_xnvctrl').enabled()
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
  )

  # build with GLX functions
  glimgui_glx = static_library(
    'glimgui_glx',
    files(
      'gl/inject_glx.cpp',
      'gl/imgui_impl_opengl3.cpp',
      'gl/imgui_hud.cpp',
      'gl/gl3w/GL/gl3w.c',
    ),
    pic : true,
    c_args : [
      pre_args,
      c_vis_args,
      no_override_init_args,
      '-DIMGUI_GLX',
    ],
    cpp_args : [
      pre_args,
      cpp_vis_args,
      '-DIMGUI_GLX',
    ],
    dependencies : [
      libimgui_core_dep,
    ],
    include_directories : [inc_common, inc_opengl],
  )

  glimgui_glx_dep = declare_dependency(
    link_with : glimgui_glx,
  )
endif

if false
#if get_option('with_wayland').enabled()
  # build with EGL functions
  glimgui_egl = static_library(
    'glimgui_egl',
    files(
      'gl/imgui_impl_opengl3.cpp',
      'gl/imgui_hud.cpp',
    ),
    pic : true,
    c_args : [
      pre_args,
      c_vis_args,
      no_override_init_args,
      '-DIMGUI_EGL',
    ],
    cpp_args : [
      pre_args,
      cpp_vis_args,
      '-DIMGUI_EGL',
    ],
    dependencies : [
      libimgui_core_dep,
    ],
    include_directories : [inc_common, inc_opengl],
  )

  glimgui_egl_dep = declare_dependency(
    link_with : glimgui_egl,
  )

endif

vklayer_mesa_overlay = shared_library(
  'MangoHud',
  util_files,
  vk_enum_to_str,
  vklayer_files,
  opengl_files,
  overlay_spv,
  vulkan_headers,
  c_args : [
    pre_args,
    c_vis_args, 
    no_override_init_args, 
    vulkan_wsi_args
    ],
  cpp_args : [
    pre_args,
    cpp_vis_args, 
    vulkan_wsi_args
    ],
  dependencies : [
    vulkan_wsi_deps, 
    libimgui_core_dep,
    glimgui_glx_dep,
    glimgui_egl_dep,
    dep_dl,
    dep_pthread,
    dep_vulkan],
  include_directories : [inc_common, inc_opengl],
  link_args : cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro']),
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
