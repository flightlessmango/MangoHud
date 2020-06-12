#!/bin/sh

VERSION=$(git describe --tags --dirty)
NAME=MangoHud-$VERSION-Source

# ensure that submodules are present
git submodule update --init
# get everything except submodules
git archive HEAD --format=tar --prefix=${NAME}/ --output=${NAME}.tar
# add imgui submodule
tar -rf ${NAME}.tar --exclude-vcs --transform="s,^modules/ImGui/src,${NAME}/modules/ImGui/src," modules/ImGui/src
# create DFSG compliant version which excludes NVML
cp ${NAME}.tar ${NAME}-DFSG.tar
tar -f ${NAME}-DFSG.tar --delete ${NAME}/include/nvml.h
# compress archives
gzip ${NAME}.tar
gzip ${NAME}-DFSG.tar
