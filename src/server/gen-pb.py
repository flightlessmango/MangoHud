#!/usr/bin/env python3

import sys
from subprocess import call
from os.path import dirname
from os import makedirs

protoc_path = sys.argv[1]
gen_path = sys.argv[2]
proto_file = sys.argv[3]
options_file = sys.argv[4]
build_dir = sys.argv[5]

source_dir = dirname(proto_file)

makedirs(build_dir, exist_ok=True)

cmdline = [protoc_path, '--plugin=protoc-gen-nanopb=' + gen_path,
      '-I' + source_dir,
      '--nanopb_out=' + '-f ' + options_file + ':' + build_dir,
      proto_file]

sys.stderr.write(" ".join(cmdline) + "\n")
exit(call(cmdline))
