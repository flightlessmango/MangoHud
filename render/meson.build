subdir('font')
gbm_dep = dependency('gbm')
render_lib = static_library(
    'render',
    files(
      'vulkan_ctx.cpp',
      'imgui_ctx.cpp',
      'font/font.cpp',
      ),
    dependencies: [vulkan_dep,
                   dearimgui_dep,
                   implot_dep,
                   gbm_dep,
                   vkbootstrap_dep,
                   spdlog_dep],
    link_with: font_lib,
    include_directories: inc_common

)

render_dep = declare_dependency(
  link_with: render_lib,
  include_directories: include_directories('.')
)
