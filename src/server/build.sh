#!/bin/sh

set -e
set -x

nanopb_generator.py -s type:FT_POINTER mangohud.proto

protoc --python_out=../gui/protos mangohud.proto


# We compile both 64-bit and 32-bit versions, to detect for example incorrect
# use of fprintf string formats (i.e. %ld instead of using %zu for size_t).
gcc -Wall -Werror -Wpedantic -pedantic-errors -Wextra -Wabi=11 -Wshadow -Wno-error=type-limits -Wno-error=sign-compare -Wformat -g -O2 -c -o common.o common.c  # To ensure it doesn't use C++, including struct initializers.
gcc -m32 -Wall -Werror -Wpedantic -pedantic-errors -Wextra -Wabi=11 -Wshadow -Wno-error=type-limits -Wno-error=sign-compare -Wformat -g -O2 -c -o common_32.o common.c  # To ensure it doesn't use C++, including struct initializers.
g++ -Wall -g -O2 -o server server.cpp common.c mangohud.pb.c -lprotobuf-nanopb
#g++ -nostdlib -fno-exceptions -nostartfiles -Wall -g -O2 -o client-demo client_demo.cpp common.c mangohud.pb.c -lprotobuf-nanopb -lc
g++ -Wall -g -O2 -o client-demo client_demo.cpp common.c mangohud.pb.c -lprotobuf-nanopb
g++ -m32 -Wall -g -O2 -o client-demo_32 client_demo.cpp common.c mangohud.pb.c -lprotobuf-nanopb
ldd client-demo | grep -E 'libgcc_s\.so|libstdc\+\+\.so' && echo "Warning: Linked to C++ runtime"


