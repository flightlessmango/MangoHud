#!/bin/sh

XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
MANGOHUD_LIB_NAME="libMangoHud.so:libMangoHud32.so"
if [ "$MANGOHUD_NODLSYM" = "1" ]; then
	MANGOHUD_LIB_NAME="libMangoHud_nodlsym.so:libMangoHud_nodlsym32.so"
fi

if [ "$#" -eq 0 ]; then
	programname=`basename "$0"`
	echo "ERROR: No program supplied"
	echo
	echo "Usage: $programname <program>"
	exit 1
fi

# Execute the program under a clean environment
# pass through the overriden LD_PRELOAD environment variables
LD_PRELOAD="${LD_PRELOAD}:${MANGOHUD_LIB_NAME}"
LD_LIBRARY_PATH="${XDG_DATA_HOME}/MangoHud"

echo $LD_LIBRARY_PATH
exec env LD_LIBRARY_PATH="$LD_LIBRARY_PATH" LD_PRELOAD="${LD_PRELOAD}" "$@"
