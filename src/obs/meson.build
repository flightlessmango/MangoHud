extra_c_args = []

# clang 13
if cc.get_id() == 'clang'
  extra_c_args += ['-Wno-typedef-redefinition']
endif

mangohud_obs = shared_module(
  'MangoHud_OBS',
  files(
    'obs_plugin.c',
    'obs_shared.c'
    ),
  dependencies: [
    obs,
    obs_frontend_api,
    dep_dl
    ],
  include_directories: ['/usr/include/obs'],
  install_dir : join_paths(get_option('prefix'), get_option('libdir'), 'obs-plugins'),
  install : true,
  c_args : extra_c_args
  )

