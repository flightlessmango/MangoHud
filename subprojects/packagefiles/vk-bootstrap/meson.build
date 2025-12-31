project('vk-bootstrap', 'cpp', default_options: ['cpp_std=c++20'])

vulkan_dep = dependency('vulkan')

vkb_inc = include_directories('src')

vkb_lib = static_library(
  'vk-bootstrap',
  sources: ['src/VkBootstrap.cpp'],
  include_directories: vkb_inc,
  dependencies: [vulkan_dep],
)

vkbootstrap_dep = declare_dependency(
  include_directories: vkb_inc,
  link_with: vkb_lib,
  dependencies: [vulkan_dep],
)
