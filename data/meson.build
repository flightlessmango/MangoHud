mandir = get_option('mandir')
datadir = get_option('datadir')
metainfo_file = files('io.github.flightlessmango.mangohud.metainfo.xml')
icon_file = files('io.github.flightlessmango.mangohud.svg')

# Validate metainfo file
ascli_exe = find_program('appstreamcli', required: false)
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
  install_dir: join_paths(datadir, 'metaino')
)

# Install icon for metainfo
install_data(
  icon_file,
  install_dir: join_paths(datadir, 'icons', 'hicolor', 'scalable', 'apps'),
)

# Install man page
install_man(
  files('mangohud.1'),
  install_dir: join_paths(mandir, 'man1'),
)
