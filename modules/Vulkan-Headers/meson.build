prog_git = find_program('git')
prog_sh = find_program('sh')
script_clone_hdrs = files('clone_headers.sh')

vk_api_xml = custom_target(
  'vk_api_xml',
  input : [],
  output : ['vk.xml'], # because output can't have segments, link vk.xml to this subdir dir in build prefix
  command : [
    prog_sh, script_clone_hdrs, prog_git, meson.current_build_dir()
  ],
)
