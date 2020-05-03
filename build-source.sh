#!/bin/sh

VERSION=$(git describe --tags)

FILE_PATTERN="--exclude-vcs --exclude-vcs-ignores ."

# default version
tar -czf MangoHud-$VERSION-Source.tar.gz $FILE_PATTERN
# DFSG compliant version, excludes NVML
tar -czf MangoHud-$VERSION-Source-DFSG.tar.gz --exclude=include/nvml.h $FILE_PATTERN
