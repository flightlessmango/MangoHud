wayland_scanner = find_program('wayland-scanner')
wayland_protocols_dep = dependency('wayland-protocols')
wayland_protocols_dir = wayland_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')
linux_dmabuf_xml = wayland_protocols_dir / 'stable/linux-dmabuf/linux-dmabuf-v1.xml'
fractional_scale_xml = wayland_protocols_dir / 'staging/fractional-scale/fractional-scale-v1.xml'
viewporter_xml = wayland_protocols_dir / 'stable/viewporter/viewporter.xml'
presentation_time_xml = wayland_protocols_dir / 'stable/presentation-time/presentation-time.xml'

linux_dmabuf_protocol = [
  custom_target(
    'linux-dmabuf-v1-client-protocol.h',
    input: linux_dmabuf_xml,
    output: 'linux-dmabuf-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
  custom_target(
    'linux-dmabuf-v1-protocol.c',
    input: linux_dmabuf_xml,
    output: 'linux-dmabuf-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
]

fractional_scale_protocol = [
  custom_target(
    'fractional-scale-v1-client-protocol.h',
    input: fractional_scale_xml,
    output: 'fractional-scale-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
  custom_target(
    'fractional-scale-v1-protocol.c',
    input: fractional_scale_xml,
    output: 'fractional-scale-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
]

viewporter_protocol = [
  custom_target(
    'viewporter-client-protocol.h',
    input: viewporter_xml,
    output: 'viewporter-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
  custom_target(
    'viewporter-protocol.c',
    input: viewporter_xml,
    output: 'viewporter-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
]

presentation_time_protocol = [
  custom_target(
    'presentation-time-client-protocol.h',
    input: presentation_time_xml,
    output: 'presentation-time-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
  custom_target(
    'presentation-time-protocol.c',
    input: presentation_time_xml,
    output: 'presentation-time-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
]

wayland_protocols = linux_dmabuf_protocol + fractional_scale_protocol + viewporter_protocol + presentation_time_protocol
wayland_ctx_sources = files('wayland_ctx.cpp')
