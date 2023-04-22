#!/bin/sh
# Simple post-install script to setcap the metrics helper

setcap="${1?SETCAP_UNSET}"
bindir="${2?BINDIR_UNSET}"
exe="${3?EXE_UNSET}"
exe_fullpath="$MESON_INSTALL_DESTDIR_PREFIX/$bindir/$exe"

euid=$(id -u)
if [ $euid != "0" ]; then
	echo "Warning: installed by unprivileged user, could not setcap $exe_fullpath" >&2
	exit 0
fi
if ! capsh --supports=CAP_PERFMON; then
    echo "Warning: build environment does not support CAP_PERFMON, you must setuid root on this platform" >&2
    exit 0
fi

"$setcap" cap_perfmon=+ep "$exe_fullpath"
