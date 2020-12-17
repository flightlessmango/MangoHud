cmake = import('cmake')

nanopb = dependency('nanopb', version : '>=0.4.2', method : 'cmake', modules : ['nanopb::protobuf-nanopb-static'], required : false)
must_regenerate_pb = nanopb.found()
if not nanopb.found()
	opt_var = cmake.subproject_options()
	opt_var.add_cmake_defines({
		#'-DCMAKE_C_COMPILER=' + ,
		'nanopb_BUILD_GENERATOR': true,
		'BUILD_SHARED_LIBS': false,
		'CMAKE_C_FLAGS': '-DPB_ENABLE_MALLOC',
		'CMAKE_POSITION_INDEPENDENT_CODE': true,
	})
	nanopb_proj = cmake.subproject('nanopb', options: opt_var)
	nanopb = nanopb_proj.dependency('protobuf-nanopb-static')
endif

rpc_common_sources = files('common.c')

if must_regenerate_pb
	protoc = find_program('protoc')
	protoc_gen_nanopb = find_program('protoc-gen-nanopb')

	gen_pb_py = meson.current_source_dir() + '/gen-pb.py'

	mangohud_pb = custom_target('gen-pb',
		input   : ['mangohud.proto', 'mangohud.options'],
		output  : ['mangohud.pb.c', 'mangohud.pb.h'],
		command : [gen_pb_py, protoc.path(), protoc_gen_nanopb.path(), '@INPUT0@', '@INPUT1@', meson.current_build_dir()])

	rpc_common_sources += mangohud_pb
else
	rpc_common_sources += 'mangohud.pb.c'
endif


#mangohud_pb = nanopb('mangohud.proto')

rpc_common = static_library(
  'common',
  rpc_common_sources,
  c_args : [
    pre_args,
  ],
  cpp_args : [
    pre_args,
  ],
  dependencies : [
    nanopb,
  ],
  #link_args : ['-lprotobuf-nanopb'],
)

#executable('server', ['server.cpp'], link_with : rpc_common, dependencies : nanopb, install : true)
executable('server', ['server.cpp'], link_with : rpc_common, install : true)
#executable('server', ['server.cpp'], dependencies : [rpc_common, nanopb], install : true)

#executable('client-demo', ['client_demo.cpp'], link_with : rpc_common, dependencies : nanopb)
