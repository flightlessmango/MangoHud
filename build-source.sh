#!/bin/sh

VERSION=$(git describe --tags --dirty)
NAME=MangoHud-$VERSION-Source

# ensure that submodules are present
git submodule update --init
# get everything except submodules
git archive HEAD --format=tar --prefix=${NAME}/ --output=${NAME}.tar
# add imgui files
tar -rf ${NAME}.tar --transform="s,^modules/ImGui/src,${NAME}/modules/ImGui/src," modules/ImGui/src/imconfig.h modules/ImGui/src/imgui.cpp modules/ImGui/src/imgui.h modules/ImGui/src/imgui_draw.cpp modules/ImGui/src/imgui_internal.h modules/ImGui/src/imgui_widgets.cpp modules/ImGui/src/imstb_rectpack.h modules/ImGui/src/imstb_textedit.h modules/ImGui/src/imstb_truetype.h
# create DFSG compliant version which excludes NVML
cp ${NAME}.tar ${NAME}-DFSG.tar
tar -f ${NAME}-DFSG.tar --delete ${NAME}/include/nvml.h
# compress archives
gzip ${NAME}.tar
gzip ${NAME}-DFSG.tar
