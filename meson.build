# Copyright © 2019 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

project('MangoHud',
  ['c', 'cpp'],
  version : 'v0.3.1',
  license : 'MIT',
  default_options : ['buildtype=release', 'c_std=c99', 'cpp_std=c++14']
)

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')
prog_python = import('python').find_installation('python3')
null_dep = dependency('', required : false)

pre_args = [
  '-D__STDC_CONSTANT_MACROS',
  '-D__STDC_FORMAT_MACROS',
  '-D__STDC_LIMIT_MACROS',
  '-DPACKAGE_VERSION="@0@"'.format(meson.project_version()),
]

# Define DEBUG for debug builds only (debugoptimized is not included on this one)
if get_option('buildtype') == 'debug'
  pre_args += '-DDEBUG'
else
  pre_args += '-DNDEBUG'
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

dep_vulkan = dependency('vulkan', required: get_option('use_system_vulkan'))
dep_pthread = dependency('threads')

# Check for generic C arguments
c_args = []
foreach a : ['-Werror=implicit-function-declaration',
             '-Werror=missing-prototypes', '-Werror=return-type',
             '-Werror=incompatible-pointer-types',
             '-fno-math-errno',
             '-fno-trapping-math', '-Qunused-arguments']
  if cc.has_argument(a)
    c_args += a
  endif
endforeach

foreach a : ['missing-field-initializers', 'format-truncation']
  if cc.has_argument('-W' + a)
    c_args += '-Wno-' + a
  endif
endforeach

c_vis_args = []
if cc.has_argument('-fvisibility=hidden')
  c_vis_args += '-fvisibility=hidden'
endif

# Check for generic C++ arguments
cpp_args = []
foreach a : ['-Werror=return-type',
             '-fno-math-errno', '-fno-trapping-math',
             '-Qunused-arguments']
  if cpp.has_argument(a)
    cpp_args += a
  endif
endforeach

# For some reason, the test for -Wno-foo always succeeds with gcc, even if the
# option is not supported. Hence, check for -Wfoo instead.

foreach a : ['non-virtual-dtor', 'missing-field-initializers', 'format-truncation']
  if cpp.has_argument('-W' + a)
    cpp_args += '-Wno-' + a
  endif
endforeach

no_override_init_args = []
foreach a : ['override-init', 'initializer-overrides']
  if cc.has_argument('-W' + a)
    no_override_init_args += '-Wno-' + a
  endif
endforeach

cpp_vis_args = []
if cpp.has_argument('-fvisibility=hidden')
  cpp_vis_args += '-fvisibility=hidden'
endif

foreach a : pre_args
  add_project_arguments(a, language : ['c', 'cpp'])
endforeach
foreach a : c_args
  add_project_arguments(a, language : ['c'])
endforeach
foreach a : cpp_args
  add_project_arguments(a, language : ['cpp'])
endforeach

# check for dl support
if is_unixy
  if cc.has_function('dlopen')
    dep_dl = null_dep
  else
    dep_dl = cc.find_library('dl')
  endif
else
  dep_dl = null_dep
endif

if dep_vulkan.found()
  datadir = get_option('datadir')
  if not datadir.startswith('/')
    datadir = get_option('prefix') / datadir
  endif
  vk_api_xml = files(datadir / 'vulkan/registry/vk.xml')
else
  vkh_sp = subproject('vulkanheaders')
  vk_api_xml = vkh_sp.get_variable('vulkan_api_xml')
  dep_vulkan = vkh_sp.get_variable('vulkanheaders_dep')
endif

vk_enum_to_str = custom_target(
  'vk_enum_to_str',
  input : ['bin/gen_enum_to_str.py', vk_api_xml],
  output : ['vk_enum_to_str.c', 'vk_enum_to_str.h'],
  command : [
    prog_python, '@INPUT0@', '--xml', '@INPUT1@',
    '--outdir', meson.current_build_dir()
  ],
)

util_files = files(
  'src/mesa/util/os_socket.c',
  'src/mesa/util/os_time.c',
)

sizeof_ptr = cc.sizeof('void*')
if sizeof_ptr == 8
  pre_args += '-DMANGOHUD_ARCH="64bit"'
elif sizeof_ptr == 4
  pre_args += '-DMANGOHUD_ARCH="32bit"'
endif

if get_option('use_system_nvml')
  cpp_nvml_args = '-DUSE_SYSTEM_NVML'
else
  cpp_nvml_args = []
endif

if ['windows', 'mingw'].contains(host_machine.system())
  subdir('modules/minhook')
  lib_dxgi        = cc.find_library('dxgi')
  lib_d3d11       = cc.find_library('d3d11')
  lib_d3dcompiler = cc.find_library('d3dcompiler')
  lib_xinput      = cc.find_library('xinput')
  windows_deps = [
    minhook_dep,
    lib_dxgi,
    lib_d3d11,
    lib_d3dcompiler,
    lib_xinput,
  ]
else
  windows_deps = null_dep
endif

subdir('modules/ImGui')
subdir('src')
