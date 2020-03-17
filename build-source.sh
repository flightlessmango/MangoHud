#!/bin/sh

VERSION=$(git describe --tags)

# include imgui specifically because it is in the gitignore
FILE_PATTERN="--exclude-vcs ./subprojects/imgui-* --exclude-vcs-ignores ."

# ensure that subprojects are downloaded
meson subprojects download

# default version
tar -czf MangoHud-$VERSION-Source.tar.gz $FILE_PATTERN
# DFSG compliant version, excludes NVML
tar -czf MangoHud-$VERSION-Source-DFSG.tar.gz --exclude=include/nvml.h $FILE_PATTERN
