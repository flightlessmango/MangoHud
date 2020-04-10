#!/bin/sh

VERSION=$(git describe --long --tags --always | sed 's/\([^-]*-g\)/r\1/;s/-/./g;s/^v//')

EXCLUDE_PATTERN="--exclude-vcs --exclude-vcs-ignores"

tar -cf v$VERSION.tar.gz $EXCLUDE_PATTERN .
tar -cf v$VERSION-DFSG.tar.gz $EXCLUDE_PATTERN --exclude=include/nvml.h .
