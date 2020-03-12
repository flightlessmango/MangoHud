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
)

opengl_files = files(
  'gl/inject.cpp',
  'gl/real_dlsym.cpp',
  'gl/imgui_impl_opengl3.cpp',
  'loaders/loader_gl.cpp',
  'gl/gl3w/GL/gl3w.c',
)
pre_args += '-DHOOK_DLSYM'
inc_opengl = include_directories('gl/gl3w')

# lib_xnvctrl = cc.find_library('XNVCtrl')
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
    dep_dl,
    dep_pthread,
    dep_vulkan],
  include_directories : [inc_common, inc_opengl],
  link_args : cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro']),
  install_dir : libdir_mangohud,
  install : true
)

if get_option('using_build_sh')
  configure_file(input : 'mangohud.json.in',
  output : '@0@.json'.format(meson.project_name()),
  configuration : {'PROJECT_NAME' : meson.project_name()},
  install : true,
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'implicit_layer.d'),
  )
else
  configure_file(input : 'mangohud.json.in',
  output : '@0@.@1@.json'.format(meson.project_name(), target_machine.cpu_family()),
  configuration : {'libdir_mangohud' : join_paths(get_option('prefix'), get_option('libdir'), 'mangohud'),
                    'PROJECT_NAME' : meson.project_name() + ' ',
                    'LAYER_SUFFIX' : target_machine.cpu_family()},
  install : true,
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'implicit_layer.d'),
  )
endif

install_data(
  files('../bin/run-mangohud-gl-pkg.sh'),
  install_dir : get_option('bindir'),
  install_mode: 'rwxr-xr-x',
  rename : ['mangohud']
)
