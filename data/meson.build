man1dir = join_paths(get_option('mandir'), 'man1')
datadir = get_option('datadir')
metainfo_file = files('io.github.flightlessmango.mangohud.metainfo.xml')
icon_file = files('io.github.flightlessmango.mangohud.svg')

# Validate metainfo file
ascli_exe = find_program('appstreamcli', required: get_option('tests'))
if ascli_exe.found()
  test('validate metainfo file',
       ascli_exe,
       args: ['validate',
              '--no-net',
              '--pedantic',
              metainfo_file]
  )
endif

# Install metainfo file
install_data(
  metainfo_file,
  install_dir: join_paths(datadir, 'metainfo')
)

# Install icon for metainfo
install_data(
  icon_file,
  install_dir: join_paths(datadir, 'icons', 'hicolor', 'scalable', 'apps'),
)

# Install man pages
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
