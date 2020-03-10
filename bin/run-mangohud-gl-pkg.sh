#!/bin/sh

MANGOHUD_LIB_NAME="libMangoHud.so"
if [ "$MANGOHUD_NODLSYM" = "1" ]; then
	MANGOHUD_LIB_NAME="libMangoHud_nodlsym.so"
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

exec env LD_PRELOAD="${LD_PRELOAD}" "$@"
