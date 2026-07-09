"""
Bluetooth phone presence detection for Raspberry Pi.
"""
import subprocess

L2PING = "/usr/bin/l2ping"
HCITOOL = "/usr/bin/hcitool"
HCICONFIG = "/usr/bin/hciconfig"
BLUETOOTHCTL = "/usr/bin/bluetoothctl"


def _run(cmd, timeout=10):
    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=timeout,
        errors="replace",
    )


def adapter_status():
    """Return (ready: bool, detail: str)."""
    result = _run([HCICONFIG, "hci0"], timeout=5)
    if result.returncode != 0:
        return False, "hci0 not found"
    text = result.stdout
    if "UP RUNNING" in text:
        return True, "up"
    if "DOWN" in text:
        return False, "down"
    return False, text.strip()[:120] or "unknown"


def ensure_bluetooth_up():
    """Best-effort bring-up without root (boot service handles rfkill)."""
    ready, detail = adapter_status()
    if ready:
        return True, detail

    _run([BLUETOOTHCTL, "power", "on"], timeout=8)
    return adapter_status()


def _l2ping(mac, use_sudo=False):
    cmd = (["sudo", L2PING] if use_sudo else [L2PING]) + ["-c", "1", "-t", "2", mac]
    subprocess.check_output(cmd, stderr=subprocess.STDOUT, timeout=8)


def _permission_error(exc):
    text = str(exc).lower()
    if hasattr(exc, "output") and exc.output:
        text += " " + exc.output.decode("utf-8", errors="replace").lower()
    return "permission" in text or "operation not permitted" in text


def _try_l2ping(mac):
    """Use cap_net_raw l2ping when available; sudo only as fallback."""
    try:
        _l2ping(mac, use_sudo=False)
        return True, ""
    except FileNotFoundError:
        return False, "l2ping not installed"
    except subprocess.CalledProcessError as exc:
        if _permission_error(exc):
            try:
                _l2ping(mac, use_sudo=True)
                return True, ""
            except Exception as sudo_exc:
                out = (getattr(sudo_exc, "output", b"") or b"").decode("utf-8", errors="replace").strip()
                return False, f"l2ping: {out or sudo_exc}"
        out = (exc.output or b"").decode("utf-8", errors="replace").strip()
        return False, f"l2ping: {out or exc}"
    except Exception as exc:
        return False, f"l2ping: {exc}"


def _hcitool_name(mac):
    out = subprocess.check_output([HCITOOL, "name", mac], stderr=subprocess.STDOUT, timeout=5)
    return bool(out.strip())


def _bluetoothctl_info(mac):
    result = _run([BLUETOOTHCTL, "info", mac], timeout=8)
    if result.returncode != 0 or not result.stdout.strip():
        return False
    text = result.stdout
    if "Device " not in text:
        return False
    if "Connected: yes" in text:
        return True
    if "Name:" in text and "not available" not in text.lower():
        return True
    if "RSSI:" in text:
        return True
    return False


def check_phone_presence(mac):
    """
    Return (is_home: bool, method: str, error: str).
    """
    ready, bt_detail = ensure_bluetooth_up()
    if not ready:
        return False, "bluetooth_down", f"Bluetooth adapter not ready ({bt_detail})"

    errors = []
    ok, err = _try_l2ping(mac)
    if ok:
        return True, "l2ping", ""
    if err:
        errors.append(err)

    try:
        if _hcitool_name(mac):
            return True, "hcitool", ""
    except subprocess.CalledProcessError as exc:
        out = (exc.output or b"").decode("utf-8", errors="replace").strip()
        errors.append(f"hcitool: {out or exc}")
    except Exception as exc:
        errors.append(f"hcitool: {exc}")

    try:
        if _bluetoothctl_info(mac):
            return True, "bluetoothctl", ""
    except Exception as exc:
        errors.append(f"bluetoothctl: {exc}")

    return False, "none", "; ".join(errors) if errors else "no method succeeded"
