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
vkbootstrap_dep = subproject('vk-bootstrap').get_variable('vkbootstrap_dep')
vulkan_layer = shared_library(
    'overlay',
    files('vulkan.cpp'),
    overlay_spv,
    dependencies: [dep_vulkan,
                   vulkan_wsi_deps,
                   spdlog_dep,
                   vkroots_dep,
                   ipc_dep,
                   dearimgui_dep],
    link_with: [ipc_static, utils_static],
    include_directories: inc_common
)
