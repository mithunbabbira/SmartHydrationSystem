# Pi monitoring and crash prevention

If the Pi freezes or you have to restart it to get smart-home working again (especially with n8n on the same Pi), use these checks.

## Why the Pi might crash or hang

1. **Low memory (OOM)**  
   n8n (Node) + Flask + serial reader + Bluetooth (`l2ping`/`hcitool`) can use a lot of RAM. If the Pi runs out of memory, the kernel can kill processes or the whole system can become unresponsive.

2. **High CPU (busy-loop)**  
   The serial reader used to spin when no data was available, using one core at 100%. This is fixed (short sleep when idle). If you still see high CPU, check `top` or `htop` for other culprits.

3. **Serial port**  
   If `/dev/serial0` is disconnected (USB unplug, ESP32 reset) or another process grabs it, the smart-home app now **reconnects automatically** instead of staying broken until restart.

4. **Disk full**  
   Logs or n8n data can fill the root filesystem and cause odd failures.

## What was changed to reduce crashes

- **Reader thread**: Sleeps when no serial data (`time.sleep(0.02)`) instead of busy-looping → much lower CPU.
- **Serial reconnection**: On read/write errors, the app closes the port and retries connecting every few seconds. No need to restart the Pi when the serial link drops.
- **Health API**: `GET /api/health` and `GET /api/health?system=true` return controller status, serial connected, and (with `system=true`) memory, load average, and disk. Use this to monitor from n8n or a dashboard.
- **Service limits**: `MemoryMax=400M` and `RestartSec=5` in `smart-home.service` so the service is restarted cleanly and doesn’t grow without bound.

## How to monitor

### 1. Health API (browser or n8n)

```bash
curl -s http://localhost:5000/api/health
curl -s "http://localhost:5000/api/health?system=true"
```

With `?system=true` you get Pi memory (MB), load average, and disk. Use this in n8n to alert when memory is low or load is high.

### 2. Health check script (cron)

Run the Pi health script periodically and optionally log to a file:

```bash
# One-off report
python3 house_automation/pi_controller/pi_health_check.py

# With API health
python3 house_automation/pi_controller/pi_health_check.py --api

# Append to log every 5 minutes (cron)
# Add to crontab (crontab -e):
# */5 * * * * python3 /home/mithun/projects/water_bottle/SmartHydrationSystem/house_automation/pi_controller/pi_health_check.py --api --log /home/mithun/pi_health.log
```

### 3. Rotating file log (recent only – no overload)

The app writes logs to a **rotating file**. **Old logs are automatically deleted** so disk use never grows:

- **Path:** `house_automation/pi_controller/logs/smart-home.log`
- **Rotation:** When the current file reaches 1 MB it rotates; the oldest backup (`.log.3`) is **removed**. You keep at most 4 files (~4 MB total). No manual cleanup needed.
- **After a crash:** Read the end of the log for exceptions or stack traces.

**View from Pi or over SSH:**

```bash
tail -n 200 /home/mithun/projects/water_bottle/SmartHydrationSystem/house_automation/pi_controller/logs/smart-home.log
```

**Or use the API (from Mac/phone):**

```bash
curl -s "http://<Pi-IP>:5000/api/debug/log?lines=300"
```

### 4. Last health snapshot (pre-crash state)

Every 60 seconds the app writes a **last_health.json** snapshot (memory, load, serial status). After a reboot you can see the state right before the process died (or before the Pi froze):

- **Path:** `house_automation/pi_controller/logs/last_health.json`
- **API:** `GET http://<Pi-IP>:5000/api/debug/last_health`

Use this to check if the Pi was low on memory or serial was disconnected before the crash.

### 5. Service logs (journald)

```bash
journalctl -u smart-home.service -f
journalctl -u smart-home.service -n 200 --no-pager
# Previous boot (after reboot):
journalctl -u smart-home.service -b -1 --no-pager
```

Look for `Serial error (will reconnect)`, `Controller failed to start`, or Python tracebacks.

### 6. Quick Pi resource check

```bash
free -m
cat /proc/loadavg
df -h /
lsof /dev/serial0   # who has the serial port
```

## How to find why the Pi crashed

After a crash or reboot, use this order:

1. **Last health snapshot** – See state right before the process died or Pi froze:
   ```bash
   cat house_automation/pi_controller/logs/last_health.json
   ```
   Or: `curl http://<Pi-IP>:5000/api/debug/last_health`  
   Check: low `mem_available_kb` → OOM; high `load_avg` → overload; `serial_connected: false` → serial issue.

2. **Rotating log** – Last lines often contain the exception or error:
   ```bash
   tail -n 300 house_automation/pi_controller/logs/smart-home.log
   ```
   Or: `curl "http://<Pi-IP>:5000/api/debug/log?lines=300"`  
   Look for: `ERROR`, `Exception`, `Traceback`, `Serial error`, `Controller failed to start`.

3. **Previous boot (journal)** – If the whole Pi rebooted:
   ```bash
   journalctl -u smart-home.service -b -1 --no-pager | tail -n 200
   ```

Logs are **automatically limited** (rotation deletes old files), so the system is not overloaded by log growth.

---

## If you still need to restart the Pi

- **Only smart-home broken**: `sudo systemctl restart smart-home.service` (no need to reboot).
- **Whole Pi unresponsive**: After reboot, check `last_health.json` and `smart-home.log` (see above) to see memory/load before the freeze; that will point to OOM or overload.

## Optional: reduce n8n load

If n8n is heavy (many workflows, large queues), consider:

- Running n8n with a lower Node memory limit.
- Moving n8n or smart-home to another device if the Pi is underpowered.
