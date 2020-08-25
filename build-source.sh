#!/bin/sh

VERSION=$(git describe --tags --dirty)
NAME=MangoHud-$VERSION-Source

# create archive via git
git archive HEAD --format=tar --prefix=${NAME}/ --output=${NAME}.tar
# create DFSG compliant version which excludes NVML
cp ${NAME}.tar ${NAME}-DFSG.tar
tar -f ${NAME}-DFSG.tar --delete ${NAME}/include/nvml.h
# compress archives
gzip ${NAME}.tar
gzip ${NAME}-DFSG.tar
