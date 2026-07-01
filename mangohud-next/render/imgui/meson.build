subdir('font')

mangohud_imgui = static_library(
    'mangohud_imgui',
    files('imgui_ctx.cpp'),
    dependencies: [dearimgui_dep, spdlog_dep, vkbootstrap_dep, implot_dep, ipc_dep],
    include_directories: inc_common
)
