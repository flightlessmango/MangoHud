#!/bin/sh

VERSION=$(git describe --tags)

EXCLUDE_PATTERN="--exclude-vcs --exclude-vcs-ignores"

tar -cf MangoHud-$VERSION-Source.tar.gz $EXCLUDE_PATTERN .
tar -cf MangoHud-$VERSION-Source-DFSG.tar.gz $EXCLUDE_PATTERN --exclude=include/nvml.h .
