#!/bin/bash
# Install RAPL powercap permissions fix for MangoHud CPU power display
# Fixes: CPU power (watts) not showing in MangoHud overlay on Intel CPUs
# Root cause: /sys/class/powercap/intel-rapl:0/energy_uj is 0400 root:root
set -e

RULE_SRC="$(dirname "$0")/99-powercap-rapl.rules"
RULE_DST="/etc/udev/rules.d/99-powercap-rapl.rules"

if [ "$(id -u)" -ne 0 ]; then
    echo "Run with sudo or pkexec"
    exit 1
fi

cp "$RULE_SRC" "$RULE_DST"
udevadm trigger --subsystem-match=powercap
echo "RAPL powercap fix installed. CPU power should now show in MangoHud."
