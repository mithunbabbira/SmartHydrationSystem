from flask import Flask, render_template, request, jsonify, send_from_directory
import threading
import time
import logging
import sys
import os
import requests
import json

# Import Controller
from controller import SerialController
import config

# Setup Logging
logging.basicConfig(level=logging.INFO)
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
    data = request.json
    cmd = data.get('cmd') # 'on', 'off', 'rgb'
    val = data.get('val') # hex for rgb, or None
    
    if controller and 'led' in controller.handlers:
        # Construct payload manually or use handler helpers if available
        # Using send_cmd directly from handler if possible, otherwise simulating input
        handler = controller.handlers['led']
        
        if cmd == 'on':
            handler.send_cmd("02100000803F", "ON")
        elif cmd == 'off':
            handler.send_cmd("021000000000", "OFF")
        elif cmd == 'rgb':
             # Expecting val to be color ID (0-8) or hex?
             # Let's support preset IDs for now as per handler
             if val is not None:
                 # 0x12 = SET_RGB. Float representation of int code.
                 import struct
                 float_hex = struct.pack('<f', int(val)).hex()
                 payload = "0212" + float_hex
                 handler.send_cmd(payload, f"Color {val}")
        
        return jsonify({"status": "ok"})
    return jsonify({"error": "Controller not ready"}), 503

# --- API: Hydration ---
@app.route('/api/hydration/cmd', methods=['POST'])
def hydration_cmd():
    data = request.json
    cmd = data.get('cmd') # 'tare' or raw hex/int
    val = data.get('val', 0) # optional value for the command
    
    if controller and 'hydration' in controller.handlers:
        handler = controller.handlers['hydration']
        mac = config.SLAVE_MACS.get('hydration', '00:00:00:00:00:00')

        # Handle Named Commands
        if cmd == 'tare':
            # CMD_TARE = 0x22
            controller.send_command(mac, "012200000000")
            return jsonify({"status": "tare_sent"})
        
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
            "last_update": h_data.get('last_update', 0)
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

def start_controller():
    global controller
    # Serial Port from Config or Default
    port = config.SERIAL_PORT
    controller = SerialController(port, 115200)
    controller.start(headless=True)
    logger.info("Controller Background Thread Started")

    # Start Scheduler Thread
    threading.Thread(target=daily_scheduler, daemon=True).start()

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
    # Start Controller in separate thread (or just before app run since it's headless)
    # Since controller.start(headless=True) returns, we can just call it.
    start_controller()
    
    # Run Flask
    # Host 0.0.0.0 to be accessible from LAN
    app.run(host='0.0.0.0', port=5000, debug=False, use_reloader=False)
