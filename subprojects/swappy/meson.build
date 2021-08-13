project('swappy', 'cpp',
    version: '1.0',
    license: 'Apache 2.0',
)

include_dirs = include_directories('.', 'include', 'common', 'vulkan')
sources = [
  'vulkan/SwappyVkBase.cpp',
  #'vulkan/swappyVk_c.cpp',
  'vulkan/SwappyVk.cpp',
  'vulkan/SwappyVkFallback.cpp',
  'vulkan/SwappyVkGoogleDisplayTiming.cpp',

  'common/CpuInfo.cpp',
#  'common/CPUTracer.cpp',
  'common/Settings.cpp',
  'common/SwappyCommon.cpp',
  'common/Thread.cpp',
  'common/SwappyDisplayManager.cpp',
  'common/ChoreographerFilter.cpp',
  'common/ChoreographerThread.cpp',
]

cpp = meson.get_compiler('cpp')
dependencies = [
  dependency('spdlog', required: true),
]

swappy = library('swappy',
    sources,
    dependencies: dependencies,
    include_directories: include_dirs,
)

swappy_dep = declare_dependency(include_directories: include_dirs, link_with: swappy)
