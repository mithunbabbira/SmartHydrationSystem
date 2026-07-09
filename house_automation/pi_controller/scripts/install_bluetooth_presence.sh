#!/bin/bash
# One-time setup: persistent Bluetooth + phone presence for smart-home.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Run with sudo:"
  echo "  sudo $0"
  exit 1
fi

ROOT="/home/mithun/projects/water_bottle/SmartHydrationSystem/house_automation/pi_controller"
REAL_USER="mithun"

echo "=== 1. Bluetooth boot + maintain services ==="
chmod 755 "$ROOT/scripts/ensure_bluetooth_up.sh"
cp "$ROOT/systemd/pi-bluetooth-up.service" /etc/systemd/system/
cp "$ROOT/systemd/pi-bluetooth-maintain.service" /etc/systemd/system/
cp "$ROOT/systemd/pi-bluetooth-maintain.timer" /etc/systemd/system/
systemctl daemon-reload
systemctl enable pi-bluetooth-up.service pi-bluetooth-maintain.timer
systemctl start pi-bluetooth-up.service
systemctl start pi-bluetooth-maintain.timer

echo "=== 2. l2ping permissions (no sudo password for presence checks) ==="
if command -v setcap >/dev/null 2>&1; then
  setcap cap_net_raw+ep /usr/bin/l2ping 2>/dev/null || true
fi
install -m 440 "$ROOT/systemd/smart-home-l2ping.sudoers" /etc/sudoers.d/smart-home-l2ping
visudo -cf /etc/sudoers.d/smart-home-l2ping

echo "=== 3. Add $REAL_USER to bluetooth group ==="
usermod -aG bluetooth "$REAL_USER" 2>/dev/null || true

echo "=== 4. Update smart-home.service ==="
cp "$ROOT/smart-home.service" /etc/systemd/system/smart-home.service
systemctl daemon-reload
systemctl restart smart-home.service

echo "=== 5. Enable Bluetooth now ==="
"$ROOT/scripts/ensure_bluetooth_up.sh"
sleep 2

echo ""
echo "=== Status ==="
hciconfig hci0 || true
echo ""
systemctl is-active pi-bluetooth-up.service smart-home.service pi-bluetooth-maintain.timer
echo ""
echo "Test presence (phone BT ON, nearby):"
sudo -u "$REAL_USER" /usr/bin/python3 - <<'PY'
import sys
sys.path.insert(0, "/home/mithun/projects/water_bottle/SmartHydrationSystem/house_automation/pi_controller")
from bluetooth_presence import check_phone_presence
ok, method, err = check_phone_presence("48:EF:1C:49:6A:E7")
print(f"  home={ok} method={method} err={err or 'none'}")
PY
echo ""
echo "Done. Bluetooth re-enables on boot and every 5 minutes if blocked."
