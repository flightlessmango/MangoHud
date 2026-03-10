project(
  'vulkan-headers',
  'c',
  license: 'Apache-2.0',
  version: '1.4.345',
  meson_version: '>=0.56.0',
)

vulkan_api_xml = files('registry/vk.xml')

vulkan_headers_dep = declare_dependency(
  include_directories: include_directories('include'),
)

meson.override_dependency('vulkanheaders', vulkan_headers_dep)
