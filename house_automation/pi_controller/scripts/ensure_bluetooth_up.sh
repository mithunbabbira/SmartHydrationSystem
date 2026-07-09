#!/bin/bash
# Bring Pi Bluetooth adapter up. Safe to run repeatedly (boot + timer).
set -euo pipefail

rfkill unblock bluetooth 2>/dev/null || true
if [ -e /sys/class/rfkill/rfkill0/soft ]; then
  echo 0 > /sys/class/rfkill/rfkill0/soft 2>/dev/null || true
fi

if command -v hciconfig >/dev/null 2>&1; then
  hciconfig hci0 up 2>/dev/null || true
fi

if command -v bluetoothctl >/dev/null 2>&1; then
  bluetoothctl power on >/dev/null 2>&1 || true
fi

exit 0
