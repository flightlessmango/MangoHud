fs = import('fs')

wayland_scanner_dep = dependency('wayland-scanner', native: true)
wayland_scanner_path = wayland_scanner_dep.get_variable(pkgconfig: 'wayland_scanner')
wayland_scanner = find_program(wayland_scanner_path, native: true)

wayland_protos = dependency('wayland-protocols',
	fallback: 'wayland-protocols',
	default_options: ['tests=false'],
)
wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

protocols = [
    wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',

	# wlroots protocols
	'wlr-layer-shell-unstable-v1.xml',
]

protocols_client_src = []
protocols_server_src = []

foreach xml : protocols
	name = fs.name(xml).replace('.xml', '')

	code = custom_target(
		name + '-protocol.c',
		input: xml,
		output: '@BASENAME@-protocol.c',
		command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
	)

	server_header = custom_target(
		name + '-protocol.h',
		input: xml,
		output: '@BASENAME@-protocol.h',
		command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
	)

	client_header = custom_target(
		name + '-client-protocol.h',
		input: xml,
		output: '@BASENAME@-client-protocol.h',
		command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
	)

	protocols_server_src += [code, server_header]
	protocols_client_src += [code, client_header]
endforeach
