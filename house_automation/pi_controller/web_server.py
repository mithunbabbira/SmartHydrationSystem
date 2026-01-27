from flask import Flask, render_template, request, jsonify, send_from_directory
import threading
import time
import logging
import sys
import os

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
    cmd = data.get('cmd') # 'tare', 'test_alert', 'test_stop'
    
    if controller and 'hydration' in controller.handlers:
        handler = controller.handlers['hydration']
        
        # We need MAC address for hydration commands usually
        # The handler's methods usually require it, or we can use the 'test' logic
        if cmd == 'tare':
            # Need to invoke handle_user_input logic or manual send
            # Use manual send logic from handler
            mac = config.SLAVE_MACS.get('hydration')
            if mac:
                controller.send_command(mac, "012200000000")
                return jsonify({"status": "tare_sent"})
        elif cmd == 'test_alert':
            # Simulate 0x52
            mac = config.SLAVE_MACS.get('hydration', '00:00:00:00:00:00')
            handler.handle_packet(0x52, 0, mac)
            return jsonify({"status": "test_alert_triggered"})
        elif cmd == 'test_stop':
            # Simulate 0x53
            mac = config.SLAVE_MACS.get('hydration', '00:00:00:00:00:00')
            handler.handle_packet(0x53, 0, mac)
            return jsonify({"status": "test_stop_triggered"})
            
    return jsonify({"status": "ok"})

def start_controller():
    global controller
    # Serial Port from Config or Default
    port = config.SERIAL_PORT
    controller = SerialController(port, 115200)
    controller.start(headless=True)
    logger.info("Controller Background Thread Started")

if __name__ == '__main__':
    # Start Controller in separate thread (or just before app run since it's headless)
    # Since controller.start(headless=True) returns, we can just call it.
    start_controller()
    
    # Run Flask
    # Host 0.0.0.0 to be accessible from LAN
    app.run(host='0.0.0.0', port=5000, debug=False, use_reloader=False)
