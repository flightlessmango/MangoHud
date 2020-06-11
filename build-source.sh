#!/bin/sh

VERSION=$(git describe --tags --dirty=+)
NAME=MangoHud-$VERSION-Source

FILE_PATTERN="--exclude-vcs --exclude-vcs-ignores ."

# Ensure that submodules are present
git submodule update --init

mkdir -p build

# default version
tar -czf build/$NAME.tar.gz $FILE_PATTERN --transform "s,^\.,$NAME,"
# DFSG compliant version, excludes NVML
tar -czf build/$NAME-DFSG.tar.gz --exclude=include/nvml.h $FILE_PATTERN --transform "s,^\.,$NAME-DFSG,"
