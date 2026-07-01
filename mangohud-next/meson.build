vkroots_dep = subproject('vkroots').get_variable('vkroots_dep')
inc_common += [
    include_directories('../include'),
    include_directories('server'),
    include_directories('utils'),
    include_directories('client'),
]
subdir('utils')
vkbootstrap_dep = subproject('vk-bootstrap').get_variable('vkbootstrap_dep')
libdrm_dep = dependency('libdrm')
subdir('ipc')
# subdir('render')
if get_option('with_server')
  subdir('render')
endif
if get_option('with_server')
  subdir('server')
endif
subdir('client')
