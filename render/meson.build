subdir('font')
gbm_dep = dependency('gbm')
static_render = static_library(
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
                   spdlog_dep,
                   utils_dep],
    link_with: font_static,
    include_directories: inc_common

)

render_dep = declare_dependency(
  link_with: static_render,
  include_directories: include_directories('.')
)
