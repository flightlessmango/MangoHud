project(
  'Dear ImGui core library',
  'cpp',
  license : 'MIT',
  version : '1.80-WIP-tables-8bc4f9ee52ef5d1a7a80c7bc2a7bdeca5f48f49a',
)

dearimgui_inc = include_directories('.')
dearimgui_src = files(
  'imgui.cpp',
  'imgui_draw.cpp',
  'imgui_widgets.cpp',
)

dearimgui_lib = static_library(
  'dearimgui',
  dearimgui_src,
  include_directories : dearimgui_inc,
)

dearimgui_dep = declare_dependency(
  link_with : dearimgui_lib,
  include_directories : dearimgui_inc,
)
