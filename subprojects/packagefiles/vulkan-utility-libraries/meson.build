project(
  'vulkan-utility-libraries',
  'c',
  license: 'Apache-2.0',
  version: '1.4.346',
  meson_version: '>=0.56.0',
)

vulkan_utility_libraries_dep = declare_dependency(
  include_directories: include_directories('include'),
)

meson.override_dependency('VulkanUtilityLibraries', vulkan_utility_libraries_dep)
