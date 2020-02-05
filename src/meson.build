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
  'overlay_params.c',
  'font_unispace.c',
  'cpu.cpp',
  'loaders/loader_nvml.cpp',
  'nvml.cpp',
  'file_utils.cpp',
  'memory.cpp',
  'nvctrl.cpp',
)

lib_xnvctrl = cc.find_library('XNVCtrl')
vklayer_mesa_overlay = shared_library(
  'MangoHud',
  util_files,
  vk_enum_to_str,
  vklayer_files,
  overlay_spv,
  vk_layer_table_helpers,
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
    lib_xnvctrl],
  include_directories : inc_common,
  link_args : cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro']),
  install : true
)

install_data(
  files('mangohud.json'),
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'implicit_layer.d'),
)
