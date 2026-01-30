#!/usr/bin/env python3
"""
Pi health check: memory, CPU load, disk, serial port usage, and smart-home API health.
Run manually or from cron to monitor why the Pi might crash (e.g. with n8n + smart-home).

Usage:
  python3 pi_health_check.py              # Print one report to stdout
  python3 pi_health_check.py --log FILE   # Append report to FILE (for cron)
  python3 pi_health_check.py --api        # Also GET localhost:5000/api/health?system=true
"""
import argparse
import subprocess
import sys
import time


def meminfo():
    try:
        with open("/proc/meminfo") as f:
            content = f.read()
    except OSError:
        return {}
    out = {}
    for line in content.splitlines():
        if ":" in line:
            key, val = line.split(":", 1)
            try:
                out[key.strip()] = int(val.strip().split()[0])  # kB
            except (ValueError, IndexError):
                pass
    return out


def loadavg():
    try:
        with open("/proc/loadavg") as f:
            return f.read().strip().split()[:3]
    except OSError:
        return []


def disk_mb(mount="/"):
    try:
        r = subprocess.run(
            ["df", "-m", mount], capture_output=True, text=True, timeout=5
        )
        if r.returncode != 0 or not r.stdout:
            return None
        line = r.stdout.strip().split("\n")[-1]
        parts = line.split()
        if len(parts) >= 4:
            return {"used_mb": int(parts[2]), "avail_mb": int(parts[3])}
    except Exception:
        pass
    return None


def serial_usage(port="/dev/serial0"):
    """Return PID and command that have the serial device open, or None."""
    try:
        r = subprocess.run(
            ["lsof", port], capture_output=True, text=True, timeout=3
        )
        if r.returncode != 0:
            return None
        lines = r.stdout.strip().split("\n")[1:]  # skip header
        if not lines:
            return None
        procs = []
        for line in lines:
            parts = line.split()
            if len(parts) >= 2:
                procs.append({"pid": parts[1], "cmd": " ".join(parts[1:])[:60]})
        return procs
    except FileNotFoundError:
        return "lsof not installed"
    except Exception as e:
        return str(e)


def api_health():
    try:
        import urllib.request
        req = urllib.request.Request(
            "http://127.0.0.1:5000/api/health?system=true",
            headers={"User-Agent": "PiHealthCheck/1.0"},
        )
        with urllib.request.urlopen(req, timeout=5) as resp:
            import json
            return json.load(resp)
    except Exception as e:
        return {"error": str(e)}


def main():
    ap = argparse.ArgumentParser(description="Pi health check for smart-home + n8n")
    ap.add_argument("--log", metavar="FILE", help="Append report to FILE (for cron)")
    ap.add_argument("--api", action="store_true", help="Also query /api/health?system=true")
    args = ap.parse_args()

    ts = time.strftime("%Y-%m-%d %H:%M:%S")
    lines = [
        f"=== Pi health @ {ts} ===",
    ]

    # Memory
    mem = meminfo()
    if mem:
        total_k = mem.get("MemTotal", 0)
        avail_k = mem.get("MemAvailable", total_k)
        total_mb = total_k // 1024
        avail_mb = avail_k // 1024
        used_mb = total_mb - avail_mb
        pct = (used_mb / total_mb * 100) if total_mb else 0
        lines.append(f"Memory: {used_mb} MB / {total_mb} MB ({pct:.0f}%) available {avail_mb} MB")
        if avail_mb < 100:
            lines.append("  WARNING: Low memory - can cause Pi freeze or OOM kill")
    else:
        lines.append("Memory: (unable to read)")

    # Load
    la = loadavg()
    if la:
        lines.append(f"Load avg: {la[0]} {la[1]} {la[2]}")
        try:
            if float(la[0]) > 4.0:
                lines.append("  WARNING: High load - Pi may be overloaded (n8n + smart-home + others)")
        except ValueError:
            pass
    else:
        lines.append("Load avg: (unable to read)")

    # Disk
    d = disk_mb("/")
    if d:
        lines.append(f"Disk /: used {d['used_mb']} MB, avail {d['avail_mb']} MB")
        if d["avail_mb"] < 500:
            lines.append("  WARNING: Low disk space")
    else:
        lines.append("Disk: (unable to read)")

    # Serial port
    ser = serial_usage()
    if ser is None:
        lines.append("Serial /dev/serial0: no process (or not open)")
    elif isinstance(ser, str):
        lines.append(f"Serial: {ser}")
    else:
        lines.append(f"Serial /dev/serial0: {len(ser)} process(es)")
        for p in ser:
            lines.append(f"  PID {p.get('pid')} {p.get('cmd', '')}")

    # Optional API health
    if args.api:
        lines.append("--- Smart-home API ---")
        h = api_health()
        if "error" in h:
            lines.append(f"  API error: {h['error']}")
        else:
            lines.append(f"  ok={h.get('ok')} controller={h.get('controller')}")
            if h.get("serial"):
                s = h["serial"]
                lines.append(f"  serial_connected={s.get('serial_connected')} port={s.get('serial_port')}")
            if h.get("system"):
                sys_h = h["system"]
                if "memory_mb" in sys_h:
                    m = sys_h["memory_mb"]
                    lines.append(f"  API memory: total={m.get('total')} MB avail={m.get('available')} MB")
                if "load_avg" in sys_h:
                    lines.append(f"  API load_avg: {sys_h['load_avg']}")

    lines.append("")
    report = "\n".join(lines)

    if args.log:
        try:
            with open(args.log, "a") as f:
                f.write(report)
        except OSError as e:
            print(report, file=sys.stderr)
            sys.exit(1)
    else:
        print(report, end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
