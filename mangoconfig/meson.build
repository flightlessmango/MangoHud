project('imgui_project', 'cpp',
        default_options : ['cpp_std=c++17'])

imgui_options = [
  'default_library=static',
  'werror=false',
  # use 'auto_features=disabled' once available: https://github.com/mesonbuild/meson/issues/5320
  'dx9=disabled',
  'dx10=disabled',
  'dx11=disabled',
  'dx12=disabled',
  'metal=disabled',
  'opengl=enabled',
  'vulkan=disabled',
  'glfw=enabled',
  'sdl2=disabled',
  'osx=disabled',
  'win=disabled',
  'allegro5=disabled',
  'webgpu=disabled'
]

mangohud_options = [
    'with_nvml=disabled',
    'with_xnvctrl=disabled',
    'with_dbus=disabled',
    'with_spdlog=disabled'
]

dearimgui_sp = subproject('imgui', default_options: imgui_options)
dearimgui_dep = dearimgui_sp.get_variable('imgui_dep')
glfw_dep = dependency('glfw3')
mangohud_sp = subproject('mangohud', default_options: mangohud_options)
# spdlog_dep = mangohud_sp.get_variable('spdlog_dep')
mangohud_static = mangohud_sp.get_variable('mangohud_static_lib')

sources = [
    'main.cpp',
    'emscripten.cpp'
]

cxx_flags = [
    '-O2',
    '-s', 'USE_WEBGL2=1',
    '-s', 'USE_GLFW=3',
    '-s', 'FULL_ES3=1',
    '-s', 'WASM=1',
    '--preload-file data',
    '--source-map',
    '-s', 'ASSERTIONS=1',
    '-s', 'TOTAL_MEMORY=512MB',
    '-s', 'USE_PTHREADS=1',
]

libs = ['-lGL']
spdlog_dep = mangohud_sp.get_variable('spdlog_dep')

executable('imgui', sources,
           dependencies : [], # add deps if any
           c_args : cxx_flags,
           objects: mangohud_static.extract_all_objects(),
        #    link_with: mangohud_static,
           include_directories: mangohud_sp.get_variable('inc_common'),
           link_args : libs,
           native: false,
           dependencies: [dearimgui_dep, glfw_dep])


configure_file(input: 'imgui.html', output: 'imgui.html', copy: true)