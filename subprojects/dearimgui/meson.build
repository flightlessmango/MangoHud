project(
  'Dear ImGui core library',
  'cpp',
  license : 'MIT',
  version : '1.78-WIP-tables-e4217412d3147c79dd0144cd1eca0d463e5dc5ed',
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
