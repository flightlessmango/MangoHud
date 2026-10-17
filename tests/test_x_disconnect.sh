#!/usr/bin/env bash
# Regression test: verify mangoapp cleanly exits with status 0
# when the underlying X server (e.g. Gamescope Xwayland) disconnects.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Resolution order:
# 1. Explicit path passed as $1
# 2. Local build outputs relative to repo root
# 3. System PATH fallback
if [ -n "${1:-}" ]; then
    MANGOAPP_BIN="$1"
elif [ -x "${REPO_ROOT}/build/src/mangoapp" ]; then
    MANGOAPP_BIN="${REPO_ROOT}/build/src/mangoapp"
elif [ -x "${REPO_ROOT}/build64/src/mangoapp" ]; then
    MANGOAPP_BIN="${REPO_ROOT}/build64/src/mangoapp"
elif [ -x "${REPO_ROOT}/build/mangoapp" ]; then
    MANGOAPP_BIN="${REPO_ROOT}/build/mangoapp"
else
    MANGOAPP_BIN="$(command -v mangoapp || true)"
fi

if [ -z "${MANGOAPP_BIN}" ] || [ ! -x "${MANGOAPP_BIN}" ]; then
    echo "Error: mangoapp executable not found."
    echo "Please build MangoHud first ('ninja -C build src/mangoapp') or specify binary path: $0 <path-to-mangoapp>"
    exit 2
fi

echo "==> Testing binary: ${MANGOAPP_BIN}"

DISPLAY_NUM=":99"

# Ensure cleanup on script exit
cleanup() {
    if [ -n "${XPID:-}" ]; then
        kill -9 "${XPID}" 2>/dev/null || true
    fi
    if [ -n "${MPID:-}" ]; then
        kill -9 "${MPID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

echo "==> Starting isolated Xwayland on ${DISPLAY_NUM}..."
Xwayland "${DISPLAY_NUM}" -ac &
XPID=$!

# Wait for Xwayland socket to appear
for i in {1..30}; do
    if [ -e "/tmp/.X11-unix/X99" ]; then break; fi
    sleep 0.1
done

if [ ! -e "/tmp/.X11-unix/X99" ]; then
    echo "Error: Xwayland failed to initialize on ${DISPLAY_NUM}"
    exit 2
fi

# Ensure Xwayland finishes keymap compilation and is fully accepting connections
sleep 0.5

echo "==> Launching mangoapp..."
DISPLAY="${DISPLAY_NUM}" "${MANGOAPP_BIN}" &
MPID=$!

# Give mangoapp time to connect to X server and initialize GLFW/X11
sleep 0.8

if ! kill -0 "${MPID}" 2>/dev/null; then
    echo "Error: mangoapp failed to start"
    exit 2
fi

echo "==> Simulating abrupt X server teardown (killing Xwayland)..."
kill -9 "${XPID}"
unset XPID

# Wait for mangoapp to react to the severed X connection
wait "${MPID}"
EXIT_CODE=$?

echo "==> mangoapp finished with exit code ${EXIT_CODE}"
unset MPID

if [ "${EXIT_CODE}" -eq 0 ]; then
    echo "SUCCESS: mangoapp handled X server disconnect cleanly (exit code 0)"
    exit 0
else
    echo "FAILURE: mangoapp crashed on X disconnect (exit code ${EXIT_CODE})"
    exit 1
fi
