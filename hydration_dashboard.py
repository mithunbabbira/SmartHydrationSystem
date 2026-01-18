# Standard imports
import os
import json
import sqlite3
import socket
import sys
from datetime import datetime
from flask import Flask, render_template, jsonify, request
from flask_socketio import SocketIO, emit
import paho.mqtt.client as mqtt

# ==================== Configuration ====================
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_USER = "babbira"
MQTT_PASSWORD = "3.14159265"
DATABASE_FILE = "hydration.db"

app = Flask(__name__)
app.config['SECRET_KEY'] = 'hydration_secret_key'

# Using 'threading' by default for better compatibility on Pi
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

# Global state
latest_telemetry = {
    "weight": 0.0,
    "delta": 0.0,
    "alert": 0,
    "today_ml": 0.0,
    "status": "online", # Assume online initially if server is up
    "last_update": None
}

# ==================== Helpers ====================
def get_ip_addresses():
    """Detect local IP addresses to help the user connect"""
    ips = []
    try:
        # Standard method
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ips.append(s.getsockname()[0])
        s.close()
    except:
        pass
    
    # Fallback to hostname
    try:
        hostname = socket.gethostname()
        host_ip = socket.gethostbyname(hostname)
        if host_ip not in ips:
            ips.append(host_ip)
    except:
        pass
    
    return ips

def get_db_stats():
    """Get summarized stats from database"""
    try:
        conn = sqlite3.connect(DATABASE_FILE)
        cursor = conn.cursor()
        today = datetime.now().date()
        
        cursor.execute('''
            SELECT COUNT(*), SUM(ABS(weight_delta)) 
            FROM hydration_events 
            WHERE DATE(timestamp) = ? AND event_type = 'drink'
        ''', (today,))
        
        result = cursor.fetchone()
        conn.close()
        return {
            "sessions": result[0] or 0,
            "total_ml": result[1] or 0.0
        }
    except Exception as e:
        print(f"DB Error: {e}")
        return {"sessions": 0, "total_ml": 0.0}

# ==================== MQTT Client ====================
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print(f"MQTT Connected (rc: {reason_code})")
        client.subscribe("hydration/#")
    else:
        print(f"MQTT Connection failed with code: {reason_code}")

def on_message(client, userdata, msg):
    global latest_telemetry
    topic = msg.topic
    payload = msg.payload.decode()
    
    # Update global state based on topic
    if topic == "hydration/telemetry":
        try:
            data = json.loads(payload)
            latest_telemetry.update(data)
            latest_telemetry["last_update"] = datetime.now().strftime("%H:%M:%S")
            socketio.emit('telemetry_update', latest_telemetry)
        except: pass
    
    elif topic == "hydration/consumption/today_ml":
        latest_telemetry["today_ml"] = float(payload)
        socketio.emit('stats_update', latest_telemetry)
        
    elif topic == "hydration/status/online":
        latest_telemetry["status"] = "online" if payload == "true" else "offline"
        socketio.emit('status_update', {"status": latest_telemetry["status"]})

mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
mqtt_client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message

# ==================== Socket.IO Events ====================
@socketio.on('connect')
def handle_connect():
    print(f"[WS] 🟢 Browser connected! Pushing initial state...")
    # Push immediate state so the browser doesn't wait for the next MQTT message
    emit('status_update', {"status": latest_telemetry["status"]})
    emit('telemetry_update', latest_telemetry)
    emit('stats_update', latest_telemetry)

# ==================== Flask Routes ====================
@app.route('/')
def index():
    print(f"[HTTP] 🌐 Dashboard page requested (index.html)")
    return render_template('index.html')

@app.route('/api/stats')
def api_stats():
    db_stats = get_db_stats()
    # Update our live cache with DB total if it's higher (standard safety)
    if db_stats["total_ml"] > latest_telemetry["today_ml"]:
        latest_telemetry["today_ml"] = db_stats["total_ml"]
        
    print(f"[HTTP] 📊 API stats requested | Cache: {latest_telemetry['today_ml']}ml | DB: {db_stats['total_ml']}ml")
    return jsonify({
        "live_ml": latest_telemetry["today_ml"],
        "db_ml": db_stats["total_ml"],
        "sessions": db_stats["sessions"]
    })

@app.route('/api/command', methods=['POST'])
def api_command():
    data = request.json
    cmd_type = data.get('command')
    value = data.get('value', 'execute')
    
    topic = f"hydration/commands/{cmd_type}"
    print(f"[API] ➡️ Sending command: {topic} = {value}")
    mqtt_client.publish(topic, value)
    return jsonify({"status": "success", "topic": topic})

# ==================== Main ====================
if __name__ == '__main__':
    # Initialize state from database so we don't start at zero
    print("[INIT] 🛠️  Syncing initial state from database...")
    initial_stats = get_db_stats()
    latest_telemetry["today_ml"] = initial_stats["total_ml"]
    print(f"[INIT] ✓ Starting with initial total: {latest_telemetry['today_ml']}ml")

    # Initialize MQTT
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        mqtt_client.loop_start()
    except Exception as e:
        print(f"❌ MQTT Connection Error: {e}")

    # Detect IPs
    my_ips = get_ip_addresses()
    
    print("\n" + "═"*50)
    print(" 🚀 HYDRATION DASHBOARD SERVER IS STARTING")
    print(" " + "═"*50)
    print(f" 🌐 Access the dashboard at:")
    for ip in my_ips:
        print(f"    👉 http://{ip}:5005")
    print(f"    👉 http://localhost:5005")
    print("\n � Note: Keep this terminal OPEN while using the dashboard.")
    print("═"*50 + "\n")
    
    # Run Flask with SocketIO
    socketio.run(app, host='0.0.0.0', port=5005, debug=False, log_output=True)
