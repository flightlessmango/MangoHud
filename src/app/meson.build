if not get_option('with_x11').enabled()
error('mangoapp also needs \'with_x11\'')
endif

subdir('protocol')
egl_dep = dependency('egl', required: true)
pre_args += '-DIMGUI_IMPL_OPENGL_LOADER_GLEW'
pre_args += '-DMANGOAPP'
mangoapp = executable(
    'mangoapp',
    files(
        'main.cpp',
    ),
    protocols_client_src,
    c_args : [
        pre_args,
        vulkan_wsi_args
    ],
    cpp_args : [
        pre_args,
        vulkan_wsi_args
    ],
    gnu_symbol_visibility : 'hidden',
    dependencies : [
        dearimgui_dep,
        dep_dl,
        dep_vulkan,
        spdlog_dep,
        dbus_dep,
        dep_wayland_client,
        glfw3_dep,
        json_dep,
        glew_dep,
        implot_dep,
        egl_dep
    ],
    include_directories : [inc_common],
    install_tag : 'mangoapp',
    link_with: mangohud_static_lib,
    link_args : link_args,
    install : true
)
