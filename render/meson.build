subdir('imgui')

render_lib = static_library(
    'render',
    files(
      'vulkan_ctx.cpp',
      'egl_ctx.cpp',
      ),
    dependencies: [vulkan_dep,
                   dearimgui_dep,
                   implot_dep,
                   dependency('gbm'),
                   vkbootstrap_dep,
                   spdlog_dep,
                   dependency('libdrm'),
                   dependency('gl'),
                   dependency('egl')],
    link_with: mangohud_imgui,
    include_directories: inc_common

)

render_dep = declare_dependency(
  link_with: render_lib,
  include_directories: include_directories('.')
)
