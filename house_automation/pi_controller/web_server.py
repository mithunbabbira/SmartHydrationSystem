from flask import Flask, render_template, request, jsonify, send_from_directory
import threading
import time
import logging
import sys
import os
import requests
import json

# Setup logging first: rotating file (recent logs only) + console
from logging_setup import setup_logging
LOG_DIR = setup_logging()

# Import Controller
from controller import SerialController
import config

logger = logging.getLogger("WebServer")

app = Flask(__name__, static_url_path='', static_folder='static', template_folder='static')

# Global Controller Instance
controller = None

@app.route('/')
def index():
    return send_from_directory('static', 'index.html')

@app.route('/<path:path>')
def static_files(path):
    return send_from_directory('static', path)

# --- API: IR Remote ---
@app.route('/api/ir/send', methods=['POST'])
def ir_send():
    data = request.json
    code = data.get('code')
    if not code:
        return jsonify({"error": "Missing code"}), 400
    
    if controller and 'ir' in controller.handlers:
        controller.handlers['ir'].send_nec(code)
        return jsonify({"status": "sent", "code": code})
    return jsonify({"error": "Controller not ready"}), 503

# --- API: LED Control ---
@app.route('/api/led/cmd', methods=['POST'])
def led_cmd():
    data = request.json or {}
    cmd = (data.get('cmd') or '').lower()
    val = data.get('val')
    mode = data.get('mode')
    speed = data.get('speed', 50)

    if controller and 'led' in controller.handlers:
        handler = controller.handlers['led']
        import struct

        if cmd == 'on':
            handler.send_cmd("02100000803F", "ON")
        elif cmd == 'off':
            handler.send_cmd("021000000000", "OFF")
        elif cmd == 'rgb':
            # Static color: val = color ID (1-8)
            if val is not None:
                float_hex = struct.pack('<f', int(val)).hex()
                payload = "0212" + float_hex
                handler.send_cmd(payload, f"Color {val}")
        elif cmd == 'mode' or cmd == 'effect':
            # Effect/mode: mode number (e.g. 37=Rainbow), speed 1-100
            m = int(mode) if mode is not None else int(val) if val is not None else None
            if m is not None and 1 <= m <= 255:
                sp = max(1, min(100, int(speed)))
                val_u32 = (m << 8) | sp
                payload = "0213" + struct.pack('<I', val_u32).hex()
                handler.send_cmd(payload, f"Mode {m} Speed {sp}")
            else:
                return jsonify({"error": "Invalid mode (1-255)"}), 400
        else:
            return jsonify({"error": "Unknown cmd"}), 400
        return jsonify({"status": "ok"})
    return jsonify({"error": "Controller not ready"}), 503


# --- API: ONO Display (OLED + RGB) ---
@app.route('/api/ono/text', methods=['POST'])
def ono_text():
    data = request.json or {}
    text = (data.get('text') or '').strip()
    duration = max(1, min(300, int(data.get('duration', 5))))
    if not text:
        return jsonify({"error": "Missing text"}), 400
    if controller and 'ono' in controller.handlers:
        controller.handlers['ono'].send_text(text, duration)
        return jsonify({"status": "ok", "text": text[:50], "duration": duration})
    return jsonify({"error": "Controller not ready"}), 503


@app.route('/api/ono/rainbow', methods=['POST'])
def ono_rainbow():
    data = request.json or {}
    duration = max(1, min(300, int(data.get('duration', 10))))
    if controller and 'ono' in controller.handlers:
        controller.handlers['ono'].send_rainbow(duration)
        return jsonify({"status": "ok", "duration": duration})
    return jsonify({"error": "Controller not ready"}), 503


@app.route('/api/ono/color', methods=['POST'])
def ono_color():
    data = request.json or {}
    r = max(0, min(255, int(data.get('r', 255))))
    g = max(0, min(255, int(data.get('g', 0))))
    b = max(0, min(255, int(data.get('b', 0))))
    duration = max(1, min(300, int(data.get('duration', 10))))
    if controller and 'ono' in controller.handlers:
        controller.handlers['ono'].send_color(r, g, b, duration)
        return jsonify({"status": "ok", "r": r, "g": g, "b": b, "duration": duration})
    return jsonify({"error": "Controller not ready"}), 503


@app.route('/api/led/raw', methods=['POST'])
def led_raw():
    data = request.json or {}
    hex_payload = (data.get('hex') or data.get('payload') or '').strip().replace('0x', '')
    if not hex_payload or not all(c in '0123456789ABCDEFabcdef' for c in hex_payload) or len(hex_payload) % 2 != 0:
        return jsonify({"error": "Invalid hex payload"}), 400
    if controller and 'led' in controller.handlers:
        controller.handlers['led'].send_cmd(hex_payload, "RAW")
        return jsonify({"status": "sent", "hex": hex_payload})
    return jsonify({"error": "Controller not ready"}), 503

# --- API: Hydration ---
@app.route('/api/hydration/cmd', methods=['POST'])
def hydration_cmd():
    data = request.json or {}
    cmd = data.get('cmd') # 'tare' or raw hex/int
    val = data.get('val', 0) # optional value for the command
    
    if controller and 'hydration' in controller.handlers:
        handler = controller.handlers['hydration']
        mac = config.SLAVE_MACS.get('hydration', '00:00:00:00:00:00')

        # Handle Named Commands
        if cmd == 'tare':
            controller.send_command(mac, "012200000000")
            return jsonify({"status": "tare_sent"})
        # Hydration slave LED (0x10): on/off without ESP32 changes
        if cmd == 'led_on':
            controller.send_command(mac, "0110" + "0000803F")  # float 1.0
            return jsonify({"status": "led_on"})
        if cmd == 'led_off':
            controller.send_command(mac, "0110" + "00000000")  # float 0.0
            return jsonify({"status": "led_off"})
        # Hydration slave Buzzer (0x11): on/off
        if cmd == 'buzzer_on':
            controller.send_command(mac, "0111" + "0000803F")
            return jsonify({"status": "buzzer_on"})
        if cmd == 'buzzer_off':
            controller.send_command(mac, "0111" + "00000000")
            return jsonify({"status": "buzzer_off"})
        if cmd == 'sync_time':
            controller.send_command(mac, "0130" + "00000000")  # slave will reply with 0x30, we send time in 0x31
            return jsonify({"status": "sync_time_sent"})
        if cmd == 'ping_presence':
            controller.send_command(mac, "0140" + "00000000")  # slave will reply with 0x40, we send presence in 0x41
            return jsonify({"status": "ping_presence_sent"})
        # Request current daily total from slave (slave replies with 0x61 so UI updates)
        if cmd == 'request_daily_total':
            import struct
            controller.send_command(mac, "0123" + struct.pack('<f', 0.0).hex())
            return jsonify({"status": "request_daily_total_sent"})

        # Handle Raw/Generic Commands
        # If cmd is integer or string representation of int
        try:
            cmd_id = int(cmd) if isinstance(cmd, int) else int(cmd, 16) if cmd.startswith('0x') else int(cmd)
            
            # Construct Generic Packet: Type (1) + Cmd + Val (Float -> Hex)
            import struct
            val_bytes = struct.pack('<f', float(val)).hex()
            payload = f"01{cmd_id:02X}{val_bytes}"
            
            controller.send_command(mac, payload)
            logger.info(f"Sent Generic Hydration CMD: {payload}")
            return jsonify({"status": "sent", "payload": payload})
            
        except ValueError:
            pass # Not a recognized number, might be a test string

        # Handle Legacy Test Strings (if needed, but Generic covers them)
        if cmd == 'test_alert':
            handler.handle_packet(0x52, 0, mac)
            return jsonify({"status": "test_alert_triggered"})
        elif cmd == 'test_stop':
            handler.handle_packet(0x53, 0, mac)
            return jsonify({"status": "test_stop_triggered"})
            
    return jsonify({"status": "ok"})

# --- API: Adafruit IO ---
@app.route('/api/aio/cmd', methods=['POST'])
def aio_cmd():
    data = request.json
    device = data.get('device') # 'neon', 'spot'
    action = data.get('action') # 'on', 'off'
    
    if device in config.LIGHT_CMDS and action in config.LIGHT_CMDS[device]:
        value = config.LIGHT_CMDS[device][action]
        try:
            headers = {
                'X-AIO-Key': config.AIO_KEY,
                'Content-Type': 'application/json'
            }
            payload = {'value': value}
            response = requests.post(config.AIO_FEED_URL, headers=headers, json=payload, timeout=5)
            
            if response.status_code == 200:
                logger.info(f"AIO Success: {device} -> {action}")
                return jsonify({"status": "sent", "aio_response": response.json()})
            else:
                logger.error(f"AIO Fail: {response.text}")
                return jsonify({"error": "AIO Error", "details": response.text}), 502
                
        except Exception as e:
            logger.error(f"AIO Request Failed: {e}")
            return jsonify({"error": str(e)}), 500
            
    return jsonify({"error": "Invalid Device or Action"}), 400


# --- API: Servo spray sequence ---
@app.route('/api/servo-spray', methods=['POST'])
def servo_spray():
    """Trigger servo + Adafruit IO spray sequence (runs in background)."""
    from servo_spray import run_sequence
    data = request.json or {}
    base_url = data.get('base_url') or data.get('servo_url')
    run_async = data.get('async', True)
    if run_async:
        def _run():
            try:
                run_sequence(base_url)
            except Exception as e:
                logger.error("Servo spray sequence error: %s", e)
        threading.Thread(target=_run, daemon=True).start()
        return jsonify({"status": "started", "message": "Sequence running in background"})
    result = run_sequence(base_url)
    return jsonify(result)


# --- API: Master serial log (data from master ESP32) ---
@app.route('/api/master/log', methods=['GET'])
def master_log():
    limit = request.args.get('limit', 200, type=int)
    limit = min(500, max(1, limit))
    if not controller:
        return jsonify({"lines": [], "error": "Controller off"})
    entries = controller.get_serial_log(limit=limit)
    return jsonify({"lines": [e["line"] for e in entries], "count": len(entries)})


# --- API: Health (for monitoring and debugging Pi crashes) ---
@app.route('/api/health', methods=['GET'])
def health():
    """Returns controller health, serial status, and optional Pi system stats."""
    include_system = request.args.get('system', 'false').lower() in ('1', 'true', 'yes')
    out = {
        "ok": controller is not None,
        "controller": "running" if controller else "not_started",
        "serial": {},
        "system": {} if include_system else None,
    }
    if controller:
        try:
            out["serial"] = controller.health()
        except Exception as e:
            out["serial"] = {"error": str(e)}
    if include_system:
        try:
            import subprocess
            # Memory (MB)
            with open("/proc/meminfo") as f:
                lines = f.read()
            mem_total = _parse_meminfo(lines, "MemTotal")
            mem_avail = _parse_meminfo(lines, "MemAvailable")
            out["system"]["memory_mb"] = {"total": mem_total, "available": mem_avail}
            # Load average
            with open("/proc/loadavg") as f:
                la = f.read().split()[:3]
            out["system"]["load_avg"] = [float(x) for x in la]
            # Disk root (optional)
            r = subprocess.run(
                ["df", "-m", "/"], capture_output=True, text=True, timeout=2
            )
            if r.returncode == 0 and r.stdout:
                parts = r.stdout.strip().split("\n")[-1].split()
                if len(parts) >= 4:
                    out["system"]["disk_root_mb"] = {"used": int(parts[2]), "avail": int(parts[3])}
        except Exception as e:
            out["system"] = {"error": str(e)}
    return jsonify(out)


def _parse_meminfo(content, key):
    for line in content.splitlines():
        if line.startswith(key + ":"):
            return int(line.split()[1]) // 1024  # kB -> MB
    return 0


# --- API: Debug / crash investigation ---
@app.route('/api/debug/last_health', methods=['GET'])
def debug_last_health():
    """Return last health snapshot (written every 60s). Use after reboot to see pre-crash state."""
    path = os.path.join(LOG_DIR, "last_health.json")
    try:
        with open(path) as f:
            return jsonify(json.load(f))
    except FileNotFoundError:
        return jsonify({"error": "No snapshot yet"}), 404
    except Exception as e:
        return jsonify({"error": str(e)}), 500


@app.route('/api/debug/log', methods=['GET'])
def debug_log():
    """Return last N lines of smart-home.log for crash debugging."""
    lines = request.args.get('lines', 200, type=int)
    lines = min(2000, max(1, lines))
    path = os.path.join(LOG_DIR, "smart-home.log")
    try:
        with open(path) as f:
            tail = f.readlines()[-lines:]
        return jsonify({"lines": tail, "path": path})
    except FileNotFoundError:
        return jsonify({"lines": [], "path": path, "error": "Log file not found"})
    except Exception as e:
        return jsonify({"error": str(e)}), 500


# --- API: System Status (Polling) ---
@app.route('/api/data', methods=['GET'])
def get_data():
    if not controller:
        return jsonify({"error": "Controller off"}), 503
    
    response = {
        "hydration": {
            "weight": 0,
            "status": "Offline"
        }
    }
    
    # Hydration Data
    if 'hydration' in controller.handlers:
        h_data = controller.handlers['hydration'].current_data
        response['hydration'] = {
            "weight": round(h_data.get('weight', 0), 1),
            "status": h_data.get('status', 'Unknown'),
            "last_update": h_data.get('last_update', 0),
            "last_drink_ml": round(h_data.get('last_drink_ml', 0), 1),
            "last_drink_time": h_data.get('last_drink_time', 0),
            "daily_total_ml": round(h_data.get('daily_total_ml', 0), 1),
        }
        
    return jsonify(response)

# --- API: Master Control ---
@app.route('/api/master/cmd', methods=['POST'])
def master_cmd():
    data = request.json
    action = data.get('action') # 'on', 'off'
    
    if action not in ['on', 'off']:
        return jsonify({"error": "Invalid Action"}), 400
        
    logger.info(f"MASTER CONTROL: Turning ALL {action.upper()}")
    
    # 1. Room Lights (AIO) - Run in Parallel Threads
    def send_aio(val):
        headers = {'X-AIO-Key': config.AIO_KEY, 'Content-Type': 'application/json'}
        try: requests.post(config.AIO_FEED_URL, headers=headers, json={'value': val}, timeout=2)
        except: pass

    # Neon Thread
    neon_val = config.LIGHT_CMDS['neon'][action]
    threading.Thread(target=send_aio, args=(neon_val,)).start()

    # Spot Thread
    spot_val = config.LIGHT_CMDS['spot'][action]
    threading.Thread(target=send_aio, args=(spot_val,)).start()

    # 2. Local Devices (IR, LED)
    if controller:
        # IR Power (Assuming Toggle, but ideally we have discrete ON/OFF)
        # User only gave 'F7F00F' (Smooth/Power?). If it is a toggle, syncing is hard.
        # Assuming F7F00F is POWER toggle.
        if action == 'off':
             # Send IR OFF
             if 'ir' in controller.handlers:
                 controller.handlers['ir'].send_nec('F740BF')
             # LED OFF
             if 'led' in controller.handlers:
                 controller.handlers['led'].send_cmd("021000000000", "OFF")
        
        elif action == 'on':
             # Send IR ON
             if 'ir' in controller.handlers:
                 controller.handlers['ir'].send_nec('F7C03F')
             # LED ON (White default?)
             if 'led' in controller.handlers:
                 controller.handlers['led'].send_cmd("02100000803F", "ON")

    return jsonify({"status": "master_sequence_started"})

def _write_health_snapshot():
    """Write last-known health to a file so after crash/reboot we can inspect it."""
    path = os.path.join(LOG_DIR, "last_health.json")
    try:
        data = {"ts": time.time()}
        # Memory
        try:
            with open("/proc/meminfo") as f:
                c = f.read()
            for line in c.splitlines():
                if line.startswith("MemAvailable:"):
                    data["mem_available_kb"] = int(line.split()[1])
                elif line.startswith("MemTotal:"):
                    data["mem_total_kb"] = int(line.split()[1])
        except Exception:
            pass
        # Load
        try:
            with open("/proc/loadavg") as f:
                data["load_avg"] = f.read().strip().split()[:3]
        except Exception:
            pass
        # Serial
        if controller:
            try:
                data["serial"] = controller.health()
            except Exception:
                data["serial"] = "error"
        with open(path, "w") as f:
            json.dump(data, f, indent=0)
    except Exception as e:
        logger.debug("Health snapshot failed: %s", e)


def _health_snapshot_loop():
    """Daemon: write health snapshot every 60s for post-crash debug."""
    while True:
        time.sleep(60)
        _write_health_snapshot()


ONO_PRICE_URL = "https://api.coingecko.com/api/v3/simple/price?ids=onocoy-token&vs_currencies=usd,inr&include_24hr_change=true"
ONO_PRICE_INTERVAL_SEC = 60   # Fetch from CoinGecko every 60s (rate limit)
ONO_PUSH_INTERVAL_SEC = 15   # Re-send last price every 15s (displays get packet soon after boot)

def _ono_price_loop():
    """Fetch ONO price every 60s. Re-push last price every 15s so new boots see data within ~15s."""
    logger.info("ONO price fetcher started (fetch %ds, push %ds)", ONO_PRICE_INTERVAL_SEC, ONO_PUSH_INTERVAL_SEC)
    time.sleep(5)  # Wait for controller
    last_price, last_change = None, 0.0
    last_fetch = -999.0  # Force first fetch immediately
    while True:
        now = time.time()
        if controller and 'ono' in controller.handlers:
            # Fetch from CoinGecko every 60s
            if now - last_fetch >= ONO_PRICE_INTERVAL_SEC:
                try:
                    r = requests.get(ONO_PRICE_URL, timeout=10)
                    if r.status_code == 200:
                        data = r.json()
                        ono = data.get("onocoy-token")
                        if ono:
                            p = ono.get("usd")
                            c = ono.get("usd_24h_change")
                            if p is not None:
                                last_price, last_change = p, (c if c is not None else 0.0)
                    elif r.status_code == 429:
                        logger.warning("ONO price API: rate limited (429)")
                except Exception as e:
                    logger.debug("ONO price fetch failed: %s", e)
                last_fetch = now
            # Send to displays every 15s (fresh or last known price)
            if last_price is not None:
                controller.handlers['ono'].send_price(last_price, last_change)
        time.sleep(ONO_PUSH_INTERVAL_SEC)


def _hydration_time_push_loop():
    """Push time to hydration slave every 5s for 60s after start so slave gets time even if its 0x30 never reaches Pi."""
    import struct
    time.sleep(2)  # Let serial/controller settle
    duration_sec = 60
    interval_sec = 5
    mac = config.SLAVE_MACS.get('hydration', '00:00:00:00:00:00')
    if mac == '00:00:00:00:00:00':
        return
    start = time.time()
    while time.time() - start < duration_sec:
        if controller and getattr(controller, 'serial_conn', None) and controller.serial_conn.is_open:
            try:
                unix_time = int(time.time())
                time_hex = struct.pack('<I', unix_time).hex()
                controller.send_command(mac, "0131" + time_hex)
                logger.info("Time push to hydration slave (startup sync)")
            except Exception as e:
                logger.debug("Time push failed: %s", e)
        time.sleep(interval_sec)


def start_controller():
    global controller
    port = config.SERIAL_PORT
    try:
        controller = SerialController(port, 115200)
        controller.start(headless=True)
        logger.info("Controller Background Thread Started")
    except Exception as e:
        logger.exception("Controller failed to start: %s", e)
        controller = None
        return
    threading.Thread(target=_hydration_time_push_loop, daemon=True).start()
    threading.Thread(target=daily_scheduler, daemon=True).start()
    threading.Thread(target=_ono_price_loop, daemon=True).start()
    _write_health_snapshot()  # once at start
    threading.Thread(target=_health_snapshot_loop, daemon=True).start()

def send_aio_global(device, action):
    if device in config.LIGHT_CMDS and action in config.LIGHT_CMDS[device]:
        val = config.LIGHT_CMDS[device][action]
        headers = {'X-AIO-Key': config.AIO_KEY, 'Content-Type': 'application/json'}
        try:
            requests.post(config.AIO_FEED_URL, headers=headers, json={'value': val}, timeout=5)
            logger.info(f"Scheduler: {device} turned {action}")
        except Exception as e:
            logger.error(f"Scheduler AIO Error: {e}")

def daily_scheduler():
    logger.info("Daily Scheduler Started")
    from datetime import datetime
    while True:
        now = datetime.now()
        # Sleep until the start of the next minute to align checks
        # But simple sleep 60 is fine for now

        # Morning Routine: 10:00 AM
        if now.hour == 10 and now.minute == 0:
            logger.info("Scheduler: Checking Morning Routine...")
            if controller and controller.is_phone_home():
                logger.info("User Home! Executing Morning Routine.")
                # LED ON
                if 'led' in controller.handlers:
                    controller.handlers['led'].send_cmd("02100000803F", "ON")
                # IR ON
                if 'ir' in controller.handlers:
                    controller.handlers['ir'].send_nec('F7C03F')
            else:
                logger.info("User Away. Skipping Morning Routine.")
            time.sleep(60) # Prevent multiple executions

        # Evening Routine: 5:00 PM (17:00)
        elif now.hour == 17 and now.minute == 0:
            logger.info("Scheduler: Checking Evening Routine...")
            if controller and controller.is_phone_home():
                logger.info("User Home! Executing Evening Routine.")
                # Neon & Spot ON
                threading.Thread(target=send_aio_global, args=('neon', 'on')).start()
                threading.Thread(target=send_aio_global, args=('spot', 'on')).start()
            else:
                logger.info("User Away. Skipping Evening Routine.")
            time.sleep(60)

        time.sleep(10) # check 

if __name__ == '__main__':
    start_controller()
    # If controller failed to start, app still runs; /api/health and /api/data will report not ready
    app.run(host='0.0.0.0', port=5000, debug=False, use_reloader=False)
