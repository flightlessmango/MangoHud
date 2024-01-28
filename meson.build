project('MangoHud',
  ['c', 'cpp'],
  version : 'v0.7.1',
  license : 'MIT',
  meson_version: '>=0.60.0',
  default_options : ['buildtype=release', 'c_std=c99', 'cpp_std=c++14', 'warning_level=2']
)

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

prog_python = import('python').find_installation('python3', modules: ['mako'])
null_dep = dependency('', required : false)

mangohud_version = vcs_tag(
  command: ['git', 'describe', '--tags', '--dirty=+'],
  input:  'version.h.in',
  output: 'version.h')

mangohud_version_dep = declare_dependency(sources : mangohud_version)

pre_args = [
  '-D__STDC_CONSTANT_MACROS',
  '-D__STDC_FORMAT_MACROS',
  '-D__STDC_LIMIT_MACROS',
  '-DPACKAGE_VERSION="@0@"'.format(meson.project_version()),
  '-DSPDLOG_COMPILED_LIB'
]

# Always set max spdlog level, handle this using MANGOHUD_LOG_LEVEL instead.
if get_option('buildtype') == 'debug'
  pre_args += '-DDEBUG'
  pre_args += '-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE'
else
  pre_args += '-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG'
endif
# TODO: this is very incomplete
is_unixy = false
if ['linux', 'cygwin', 'gnu'].contains(host_machine.system())
  pre_args += '-D_GNU_SOURCE'
  pre_args += '-DHAVE_PTHREAD'
  is_unixy = true
endif

if get_option('glibcxx_asserts')
  pre_args += '-D_GLIBCXX_ASSERTIONS'
endif

# Check for GCC style atomics
if cc.compiles('''#include <stdint.h>
                  int main() {
                    struct {
                      uint64_t *v;
                    } x;
                    return (int)__atomic_load_n(x.v, __ATOMIC_ACQUIRE) &
                           (int)__atomic_add_fetch(x.v, (uint64_t)1, __ATOMIC_ACQ_REL);

                  }''',
               name : 'GCC atomic builtins')
  pre_args += '-DUSE_GCC_ATOMIC_BUILTINS'
endif

# Not in C99, needs POSIX
if cc.compiles('''
                 #define _GNU_SOURCE
                 #include <time.h>
                  int main() {
                    struct timespec ts;
                    return timespec_get(&ts, TIME_UTC);

                  }''',
               name : 'Supports timespec_get')
  pre_args += '-DHAVE_TIMESPEC_GET'
endif

# Check for GCC style builtins
foreach b : ['bswap32', 'bswap64', 'clz', 'clzll', 'ctz', 'expect', 'ffs',
             'ffsll', 'popcount', 'popcountll', 'unreachable']
  if cc.has_function(b)
    pre_args += '-DHAVE___BUILTIN_@0@'.format(b.to_upper())
  endif
endforeach

vulkan_wsi_args = []
vulkan_wsi_deps = []

if is_unixy
  dep_x11 = dependency('x11', required: get_option('with_x11'))
  dep_wayland_client = dependency('wayland-client',
      required: get_option('with_wayland'), version : '>=1.11')
  dbus_dep = dependency('dbus-1', required: get_option('with_dbus')).partial_dependency(compile_args : true, includes : true)
else
  dep_x11 = null_dep
  dep_wayland_client = null_dep
  dbus_dep = null_dep
endif

if dep_x11.found()
  vulkan_wsi_args += ['-DVK_USE_PLATFORM_XLIB_KHR']
  vulkan_wsi_deps += dep_x11.partial_dependency(compile_args : true, includes : true)
endif
if dep_wayland_client.found()
  vulkan_wsi_args += ['-DVK_USE_PLATFORM_WAYLAND_KHR']
  vulkan_wsi_deps += dep_wayland_client
endif

if is_unixy and not dep_x11.found() and not dep_wayland_client.found()
  error('At least one of "with_x11" and "with_wayland" should be enabled')
endif

inc_common = [
    include_directories('include'),
]

dep_pthread = dependency('threads')

add_project_arguments(
  cc.get_supported_arguments([
    '-Werror=implicit-function-declaration',
    '-Werror=missing-declarations',
    '-Werror=missing-prototypes',
    '-Werror=return-type',
    '-Werror=incompatible-pointer-types',
    '-Wno-unused-parameter',
    '-Qunused-arguments',
    '-fno-math-errno',
    '-fno-trapping-math',
    '-Wno-missing-field-initializers',
  ]), language : ['c'],
)

add_project_arguments(
  cpp.get_supported_arguments([
    '-Werror=missing-declarations',
    '-Werror=return-type',
    '-Wno-unused-parameter',
    '-Qunused-arguments',
    '-fno-math-errno',
    '-fno-trapping-math',
    '-Wno-non-virtual-dtor',
    '-Wno-missing-field-initializers',
  ]), language : ['cpp'],
)

foreach a : pre_args
  add_project_arguments(a, language : ['c', 'cpp'])
endforeach

# check for dl support
if is_unixy
  if cc.has_function('dlopen')
    dep_dl = null_dep
  else
    dep_dl = cc.find_library('dl')
  endif
# check for linking with rt by default
  if cc.has_function('clock_gettime')
    dep_rt = null_dep
  else
    dep_rt = cc.find_library('rt')
  endif
else
  dep_dl = null_dep
  dep_rt = null_dep
endif

vkh_sp = subproject('vulkan-headers')
vk_api_xml = vkh_sp.get_variable('vulkan_api_xml')
dep_vulkan = vkh_sp.get_variable('vulkan_headers_dep')

vk_enum_to_str = custom_target(
  'vk_enum_to_str',
  input : ['bin/gen_enum_to_str.py', vk_api_xml],
  output : ['vk_enum_to_str.c', 'vk_enum_to_str.h'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--outdir', meson.current_build_dir()
  ],
)

imgui_options = [
  'default_library=static',
  'werror=false',
  # use 'auto_features=disabled' once available: https://github.com/mesonbuild/meson/issues/5320
  'dx9=disabled',
  'dx10=disabled',
  'dx11=disabled',
  'dx12=disabled',
  'metal=disabled',
  'opengl=disabled',
  'vulkan=disabled',
  'glfw=disabled',
  'sdl2=disabled',
  'osx=disabled',
  'win=disabled',
  'allegro5=disabled',
  'webgpu=disabled',
  'sdl_renderer=disabled'
]

sizeof_ptr = cc.sizeof('void*')
if sizeof_ptr == 8
  pre_args += '-DMANGOHUD_ARCH="64bit"'
elif sizeof_ptr == 4
  pre_args += '-DMANGOHUD_ARCH="32bit"'
endif

if get_option('mangoapp')
  imgui_options += [
    'opengl=enabled',
    'glfw=enabled',
  ]
endif

dearimgui_sp = subproject('imgui', default_options: imgui_options)
dearimgui_dep = dearimgui_sp.get_variable('imgui_dep')

if is_unixy
implot_sp = subproject('implot', default_options: ['default_library=static'])
implot_dep = implot_sp.get_variable('implot_dep')
else
implot_dep = null_dep
implot_lib = static_library('nulllib', [])
endif

spdlog_dep = cpp.find_library('spdlog', required: get_option('use_system_spdlog'))
if not spdlog_dep.found()
  spdlog_sp = subproject('spdlog', default_options: [
    'default_library=static',
    'compile_library=true',
    'werror=false',
    'tests=disabled',
    'external_fmt=disabled',
    'std_format=disabled'
  ])
  spdlog_dep = spdlog_sp.get_variable('spdlog_dep')
else
  spdlog_dep = dependency('spdlog', required: true)
endif

if ['windows', 'mingw'].contains(host_machine.system())
  minhook_sp = subproject('minhook')
  minhook_dep = minhook_sp.get_variable('minhook_dep')
  windows_deps = [
    minhook_dep,
  ]
else
  windows_deps = null_dep
endif

if get_option('mangoapp') or get_option('mangoapp_layer')
  glfw3_dep = dependency('glfw3')
  glew_dep = dependency('glew')
endif

json_dep = dependency('nlohmann_json')
subdir('src')

if get_option('include_doc')
  subdir('data')
endif

if get_option('tests').enabled()
  cmocka_dep = dependency('cmocka', fallback: ['cmocka', 'cmocka_dep'])

  e = executable('amdgpu', 'tests/test_amdgpu.cpp',
    files(
      'src/amdgpu.cpp',
      'src/cpu.cpp',
      'src/gpu.cpp',
      'src/mesa/util/os_time.c',
      'src/file_utils.cpp',
    ),
    cpp_args: ['-DTEST_ONLY'],
    dependencies: [
      cmocka_dep,
      spdlog_dep,
      dearimgui_dep
    ],
    include_directories: inc_common)

  test('test amdgpu', e, workdir : meson.project_source_root() + '/tests')

endif

# install helper sripts
if get_option('mangoplot').enabled()
  subdir('bin')
endif
