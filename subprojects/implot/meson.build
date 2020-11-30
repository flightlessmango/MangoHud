project(
  'ImPlot',
  'cpp',
  license : 'MIT',
  version : 'v0.9-WIP-95eb2ea78dff93197c233f95ee394b9ba32b34c1',
)

implot_inc = include_directories('.')
implot_src = files(
  'implot.cpp',
  'implot_items.cpp',
)

dearimgui_inc = subproject('dearimgui').get_variable('dearimgui_inc')
implot_lib = static_library(
  'implot',
  implot_src,
  include_directories : [implot_inc, dearimgui_inc],
)

implot_dep = declare_dependency(
  link_with : implot_lib,
  include_directories : implot_inc,
)
