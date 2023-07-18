#!/bin/sh

VERSION=$(git describe --tags --dirty)
NAME=MangoHud-${VERSION}
TAR_NAME=${NAME}-Source.tar.xz
DFSG_TAR_NAME=${NAME}-Source-DFSG.tar.xz

# remove existing files
rm -rf sourcedir
rm -rf ${NAME}
rm -f ${TAR_NAME}
rm -f ${DFSG_TAR_NAME}

# create tarball with meson
meson setup sourcedir
meson dist --formats=xztar --include-subprojects --no-tests -C sourcedir
mv sourcedir/meson-dist/*.tar.xz ${TAR_NAME}

# create DFSG compliant version
# unpack since tarball is compressed
mkdir ${NAME}
tar -xf ${TAR_NAME} --strip 1 -C ${NAME}
# nvml.h is not DFSG compliant
rm ${NAME}/include/nvml.h
# minhook not needed
rm -r ${NAME}/modules/minhook
# spdlog from system
rm -r ${NAME}/subprojects/spdlog-*
# nlohmann_json from system
rm -r ${NAME}/subprojects/nlohmann_json-*
# remove some vulkan clutter
rm -r ${NAME}/subprojects/Vulkan-Headers-*/cmake ${NAME}/subprojects/Vulkan-Headers-*/BUILD.gn
# remove some dear imgui clutter
rm -rf ${NAME}/subprojects/imgui-*/examples ${NAME}/subprojects/imgui-*/misc
# compress new sources
tar -cJf ${DFSG_TAR_NAME} ${NAME}

# cleanup
rm -r sourcedir
rm -r ${NAME}
