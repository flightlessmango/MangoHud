man1dir = join_paths(get_option('mandir'), 'man1')

install_man(
  files('mangohud.1'),
  install_dir: man1dir,
)

if get_option('mangoapp')
  install_man(
    files('mangoapp.1'),
    install_dir: man1dir,
  )
endif
