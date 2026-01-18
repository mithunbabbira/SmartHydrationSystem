#!/usr/bin/env python3
"""
Smart Hydration System - Web Dashboard
========================================
Real-time Flask web dashboard with WebSocket updates
"""

import os
import json
import sqlite3
import socket
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
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

# Global state (all data from ESP32)
latest_telemetry = {
    # Weight data
    "weight": 0.0,
    "weight_previous": 0.0,
    "delta": 0.0,
    
    # Consumption
    "today_ml": 0.0,
    "interval_ml": 0.0,
    "last_drink_time": "Never",
    
    # Alerts
    "alert_level": 0,
    "alert_triggered_time": "Never",
    "snooze_active": False,
    "bottle_missing": False,
    "daily_refill_check": "Unknown",
    
    # Presence & Status
    "presence": "unknown",        # home/away/unknown
    "system_online": False,
    "bluetooth_status": "disconnected",
    
    # System info
    "last_update": None
}

# ==================== Helpers ====================
def get_ip_addresses():
    """Detect local IP addresses"""
    ips = []
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ips.append(s.getsockname()[0])
        s.close()
    except:
        pass
    
    try:
        hostname = socket.gethostname()
        host_ip = socket.gethostbyname(hostname)
        if host_ip not in ips:
            ips.append(host_ip)
    except:
        pass
    
    return ips or ["localhost"]

def get_db_stats():
    """Get stats from database"""
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
        print(f"✓ MQTT Connected")
        client.subscribe("hydration/#")
        print(f"✓ Subscribed to hydration/#")
    else:
        print(f"✗ MQTT Connection failed: {reason_code}")

def on_message(client, userdata, msg):
    global latest_telemetry
    topic = msg.topic
    payload = msg.payload.decode()
    
    print(f"[MQTT] {topic}: {payload}")
    
    try:
        # ===== Telemetry (batched weight data) =====
        if topic == "hydration/telemetry":
            data = json.loads(payload)
            latest_telemetry["weight"] = data.get("weight", 0.0)
            latest_telemetry["delta"] = data.get("delta", 0.0)
            latest_telemetry["alert_level"] = data.get("alert", 0)
            latest_telemetry["last_update"] = datetime.now().strftime("%H:%M:%S")
            socketio.emit('telemetry_update', latest_telemetry)
        
        # ===== Weight Topics =====
        elif topic == "hydration/weight/current":
            latest_telemetry["weight"] = float(payload)
            socketio.emit('telemetry_update', latest_telemetry)
        
        elif topic == "hydration/weight/previous":
            latest_telemetry["weight_previous"] = float(payload)
        
        elif topic == "hydration/weight/delta":
            latest_telemetry["delta"] = float(payload)
        
        # ===== Consumption Topics =====
        elif topic == "hydration/consumption/today_ml":
            latest_telemetry["today_ml"] = float(payload)
            socketio.emit('stats_update', latest_telemetry)
        
        elif topic == "hydration/consumption/interval_ml":
            latest_telemetry["interval_ml"] = float(payload)
        
        elif topic == "hydration/consumption/last_drink":
            latest_telemetry["last_drink_time"] = payload
        
        # ===== Alert Topics =====
        elif topic == "hydration/alerts/level":
            latest_telemetry["alert_level"] = int(payload)
            socketio.emit('alert_update', latest_telemetry)
        
        elif topic == "hydration/alerts/triggered":
            latest_telemetry["alert_triggered_time"] = payload
        
        elif topic == "hydration/alerts/snooze_active":
            latest_telemetry["snooze_active"] = (payload.lower() == "true")
            socketio.emit('telemetry_update', latest_telemetry)
        
        elif topic == "hydration/alerts/bottle_missing":
            latest_telemetry["bottle_missing"] = (payload == "active")
            socketio.emit('telemetry_update', latest_telemetry)
        
        elif topic == "hydration/alerts/daily_refill_check":
            latest_telemetry["daily_refill_check"] = payload
        
        # ===== Status Topics =====
        elif topic == "hydration/status/online":
            latest_telemetry["system_online"] = (payload == "true")
            socketio.emit('status_update', latest_telemetry)
        
        elif topic == "hydration/status/bluetooth":
            latest_telemetry["bluetooth_status"] = payload
            # Map to presence
            if payload == "connected":
                latest_telemetry["presence"] = "home"
            elif payload == "disconnected":
                latest_telemetry["presence"] = "away"
            else:
                latest_telemetry["presence"] = "unknown"
            
            print(f"[PRESENCE] BT Status: {payload} → Presence: {latest_telemetry['presence']}")
            socketio.emit('presence_update', latest_telemetry)
        
    except Exception as e:
        print(f"Error processing {topic}: {e}")

mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
mqtt_client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message

# ==================== Socket.IO Events ====================
@socketio.on('connect')
def handle_connect():
    print(f"[WS] Browser connected - pushing initial state")
    emit('status_update', latest_telemetry)
    emit('telemetry_update', latest_telemetry)
    emit('stats_update', latest_telemetry)
    emit('presence_update', latest_telemetry)

@socketio.on('disconnect')
def handle_disconnect():
    print(f"[WS] Browser disconnected")

# ==================== Flask Routes ====================
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/stats')
def api_stats():
    db_stats = get_db_stats()
    if db_stats["total_ml"] > latest_telemetry["today_ml"]:
        latest_telemetry["today_ml"] = db_stats["total_ml"]
    
    return jsonify({
        "live_ml": latest_telemetry["today_ml"],
        "db_ml": db_stats["total_ml"],
        "sessions": db_stats["sessions"],
        "presence": latest_telemetry["presence"],
        "system_online": latest_telemetry["system_online"]
    })

@app.route('/api/telemetry')
def api_telemetry():
    """Get all current telemetry"""
    return jsonify(latest_telemetry)

@app.route('/api/command', methods=['POST'])
def api_command():
    data = request.json
    cmd_type = data.get('command')
    value = data.get('value', 'execute')
    
    topic = f"hydration/commands/{cmd_type}"
    print(f"[API] Sending command: {topic} = {value}")
    mqtt_client.publish(topic, value)
    return jsonify({"status": "success", "topic": topic, "value": value})

# ==================== Main ====================
if __name__ == '__main__':
    # Initialize from database
    print("[INIT] Syncing initial state from database...")
    initial_stats = get_db_stats()
    latest_telemetry["today_ml"] = initial_stats["total_ml"]
    print(f"[INIT] ✓ Starting with: {latest_telemetry['today_ml']}ml")

    # Connect to MQTT
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        mqtt_client.loop_start()
        print("✓ MQTT client started")
    except Exception as e:
        print(f"✗ MQTT Error: {e}")

    # Display access URLs
    my_ips = get_ip_addresses()
    print("\n" + "="*50)
    print(" 🚀 HYDRATION DASHBOARD SERVER")
    print(" " + "="*50)
    print(" 🌐 Access at:")
    for ip in my_ips:
        print(f"    → http://{ip}:5005")
    print("\n 💡 Keep this terminal OPEN")
    print("="*50 + "\n")
    
    # Run server
    socketio.run(app, host='0.0.0.0', port=5005, debug=False, log_output=True)
