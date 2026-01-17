#!/usr/bin/env python3
"""
Smart Hydration System - Pi5 MQTT Server
=========================================

Lightweight MQTT server with optional database logging.
All files stored in current directory.

Author: Babbira
"""

import paho.mqtt.client as mqtt
import sqlite3
import os
import json
from datetime import datetime

# ==================== Configuration ====================
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_USER = "babbira"
MQTT_PASSWORD = "3.14159265"

# Files in current directory
DATABASE_FILE = "hydration.db"

# Database logging (set to False to disable)
ENABLE_DATABASE = True

# Notification settings
ENABLE_NOTIFICATIONS = False

# Global state for latest telemetry
last_weight = 0.0
last_delta = 0.0
last_alert = 0
last_weight_time = None

# ==================== Database Setup ====================
def init_database():
    """Initialize SQLite database with required tables"""
    if not ENABLE_DATABASE:
        print("Database logging disabled")
        return
    
    try:
        conn = sqlite3.connect(DATABASE_FILE)
        cursor = conn.cursor()
        
        # Main events table
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS hydration_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                event_type TEXT,
                weight_delta REAL,
                alert_level INTEGER,
                user_home BOOLEAN
            )
        ''')
        
        # Daily summary table
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS daily_summary (
                date DATE PRIMARY KEY,
                total_ml_consumed REAL,
                drinking_sessions INTEGER,
                alerts_triggered INTEGER
            )
        ''')
        
        # Raw telemetry table
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS telemetry (
                timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                topic TEXT,
                payload TEXT
            )
        ''')
        
        conn.commit()
        conn.close()
        print(f"✓ Database initialized: {DATABASE_FILE}")
    except Exception as e:
        print(f"⚠ Database init failed: {e}")

# ==================== MQTT Callbacks ====================
def on_connect(client, userdata, flags, rc):
    """Callback when connected to MQTT broker"""
    if rc == 0:
        print("✓ Connected to MQTT broker")
        client.subscribe("hydration/#")
        print("✓ Subscribed to hydration/#")
    else:
        print(f"✗ Connection failed with code {rc}")

def on_message(client, userdata, msg):
    """Callback when MQTT message received"""
    topic = msg.topic
    payload = msg.payload.decode('utf-8')
    
    # Log to console
    timestamp = datetime.now().strftime("%H:%M:%S")
    print(f"[{timestamp}] {topic}: {payload}")
    
    # Store in database if enabled
    if ENABLE_DATABASE:
        try:
            conn = sqlite3.connect(DATABASE_FILE)
            cursor = conn.cursor()
            cursor.execute('INSERT INTO telemetry (topic, payload) VALUES (?, ?)', 
                           (topic, payload))
            conn.commit()
            conn.close()
        except Exception as e:
            print(f"  ⚠ DB write failed: {e}")
    
    # Update global weight if telemetry received
    if topic == "hydration/telemetry":
        try:
            data = json.loads(payload)
            global last_weight, last_delta, last_alert, last_weight_time
            last_weight = data.get('weight', 0.0)
            last_delta = data.get('delta', 0.0)
            last_alert = data.get('alert', 0)
            last_weight_time = datetime.now()
        except Exception as e:
            print(f"  ⚠ Telemetry parse error: {e}")
    
    # Process specific topics
    try:
        if topic == "hydration/weight/delta":
            delta = float(payload)
            if delta <= -90 and delta >= -110:
                print(f"  ✅ User drank water: {abs(delta):.1f}ml")
            elif delta >= 100:
                print(f"  🔄 Bottle refilled: +{delta:.1f}g")
        
        elif topic == "hydration/alerts/level":
            level = int(payload)
            if level == 3:
                print("  🚨 ALERT LEVEL 3: User not responding!")
            elif level == 2:
                print("  ⚠️ Alert Level 2: Buzzer")
            elif level == 1:
                print("  🔔 Alert Level 1: LED")
        
        elif topic == "hydration/alerts/daily_refill_check":
            if payload == "passed":
                print("  ✅ Daily refill check PASSED")
            else:
                print("  ⚠️ Daily refill check FAILED - bottle low!")
        
        elif topic == "hydration/status/bluetooth":
            if payload == "connected":
                print("  👤 User at home")
            else:
                print("  🚶 User away")
    
    except Exception as e:
        print(f"  ⚠ Processing error: {e}")

# ==================== Remote Control Functions ====================
def send_command(client, command_topic, payload):
    """Send command to ESP32"""
    full_topic = f"hydration/commands/{command_topic}"
    client.publish(full_topic, payload)
    print(f"✓ Command sent: {command_topic} = {payload}")

# ==================== Analytics Functions ====================
def get_today_stats():
    """Get today's consumption statistics"""
    if not ENABLE_DATABASE:
        print("Database disabled - no stats available")
        return None
    
    try:
        conn = sqlite3.connect(DATABASE_FILE)
        cursor = conn.cursor()
        
        today = datetime.now().date()
        
        cursor.execute('''
            SELECT 
                COUNT(*) as sessions,
                SUM(ABS(weight_delta)) as total_ml
            FROM hydration_events
            WHERE DATE(timestamp) = ? AND event_type = 'drink'
        ''', (today,))
        
        result = cursor.fetchone()
        conn.close()
        
        sessions = result[0] or 0
        total_ml = result[1] or 0
        
        return {
            'date': str(today),
            'sessions': sessions,
            'total_ml': total_ml,
            'goal_met': total_ml >= 2000
        }
    except Exception as e:
        print(f"Stats error: {e}")
        return None

def print_stats():
    """Print current statistics"""
    stats = get_today_stats()
    if not stats:
        return
    
    print("\n" + "="*50)
    print("📊 TODAY'S HYDRATION STATS")
    print("="*50)
    print(f"Date: {stats['date']}")
    print(f"Water consumed: {stats['total_ml']:.1f} ml")
    print(f"Drinking sessions: {stats['sessions']}")
    print(f"Daily goal (2000ml): {'✅ MET' if stats['goal_met'] else '❌ NOT MET'}")
    print("="*50 + "\n")

# ==================== Main Program ====================
def main():
    """Main program loop"""
    print("\n" + "="*50)
    print("  Smart Hydration System - Pi5 Server")
    print("="*50)
    print(f"Working directory: {os.getcwd()}")
    print(f"Database logging: {'Enabled' if ENABLE_DATABASE else 'Disabled'}")
    print("="*50 + "\n")
    
    # Initialize database
    if ENABLE_DATABASE:
        init_database()
    
    # Setup MQTT client
    client = mqtt.Client()
    client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    client.on_connect = on_connect
    client.on_message = on_message
    
    # Connect to broker
    try:
        print(f"Connecting to MQTT broker at {MQTT_BROKER}:{MQTT_PORT}...")
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        print("✓ MQTT client started")
    except Exception as e:
        print(f"✗ Failed to connect to MQTT broker: {e}")
        print("\nMake sure Mosquitto is running:")
        print("  sudo systemctl status mosquitto")
        return
    
    # Start MQTT loop in background
    client.loop_start()
    
    # Interactive command loop
    print("\n📋 Commands: stats, weight, tare, led, buzzer, snooze, reset, reboot, quit\n")
    
    try:
        while True:
            try:
                cmd = input("hydration> ").strip().lower()
            except EOFError:
                break
            
            if cmd == "stats":
                print_stats()
            
            elif cmd == "weight":
                if last_weight_time is not None:
                    time_ago = (datetime.now() - last_weight_time).total_seconds()
                    print(f"\n⚖️  Current Bottle Weight: {last_weight:.1f}g")
                    print(f"   Last Delta: {last_delta:+.1f}g")
                    print(f"   Alert Level: {last_alert}")
                    print(f"   Updated: {time_ago:.0f}s ago\n")
                else:
                    print("\n⚠️  No weight data received from ESP32 yet. Please wait (updates every 30s).\n")
            
            elif cmd == "tare":
                send_command(client, "tare_scale", "execute")
            
            elif cmd == "led":
                send_command(client, "trigger_led", "on")
            
            elif cmd == "buzzer":
                send_command(client, "trigger_buzzer", "on")
            
            elif cmd == "snooze":
                send_command(client, "snooze", "15")
            
            elif cmd == "reboot":
                confirm = input("⚠️  Reboot ESP32? (y/n): ")
                if confirm.lower() == 'y':
                    send_command(client, "reboot", "execute")
            
            elif cmd == "reset":
                confirm = input("⚠️  Reset daily consumption in ESP32 memory? (y/n): ")
                if confirm.lower() == 'y':
                    send_command(client, "reset_today", "execute")
            
            elif cmd in ["quit", "exit", "q"]:
                break
            
            elif cmd == "help" or cmd == "?":
                print("\n📋 Available Commands:")
                print("  stats   - Show today's statistics")
                print("  weight  - Show current bottle weight")
                print("  tare    - Remotely tare the scale")
                print("  led     - Test LED")
                print("  buzzer  - Test buzzer")
                print("  snooze  - Activate snooze (15 min)")
                print("  reset   - Reset today's consumption to 0ml")
                print("  reboot  - Reboot ESP32")
                print("  quit    - Exit program\n")
            
            elif cmd:
                print(f"❓ Unknown command: {cmd} (type 'help' for commands)")
    
    except KeyboardInterrupt:
        print("\n\n👋 Shutting down...")
    
    finally:
        client.loop_stop()
        client.disconnect()
        print("✓ Shutdown complete")

if __name__ == "__main__":
    main()
