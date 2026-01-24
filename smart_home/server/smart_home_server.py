#!/usr/bin/env python3
"""
Smart Home Central Server (Pi5)
===============================
Handles UART communication with ESP32 Gateway and Bluetooth presence detection.
"""

import serial
import json
import time
import subprocess
import threading
import sqlite3
import os
from datetime import datetime

# --- Configuration ---
SERIAL_PORT = "/dev/ttyS0" # Change to /dev/ttyUSB0 if using USB-Serial
BAUD_RATE = 115200
PHONE_MAC = "48:EF:1C:49:6A:E7" # User's device
DATABASE_FILE = "smart_home.db"

# --- Globals ---
is_user_home = False
latest_telemetry = {}
serial_conn = None

# --- Database ---
def init_db():
    conn = sqlite3.connect(DATABASE_FILE)
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS events 
                 (timestamp DATETIME, src INTEGER, type TEXT, data TEXT)''')
    conn.commit()
    conn.close()

def log_event(src, event_type, data):
    try:
        conn = sqlite3.connect(DATABASE_FILE)
        c = conn.cursor()
        c.execute("INSERT INTO events VALUES (?, ?, ?, ?)",
                  (datetime.now(), src, event_type, json.dumps(data)))
        conn.commit()
        conn.close()
    except Exception as e:
        print(f"DB Error: {e}")

# --- Presence Detection ---
def check_presence():
    global is_user_home
    while True:
        try:
            # Using hcitool name is low-impact presence check
            result = subprocess.check_output(["hcitool", "name", PHONE_MAC], timeout=5)
            new_state = bool(result.strip())
            
            if new_state != is_user_home:
                is_user_home = new_state
                print(f"Presence Change: {'HOME' if is_user_home else 'AWAY'}")
                # Forward presence to Hydration Slave if needed
                send_command(1, "presence", "home" if is_user_home else "away")
                
        except Exception as e:
            # hcitool might return error if device not found
            if is_user_home:
                is_user_home = False
                print("Presence Change: AWAY (Detection Error/Timeout)")
        
        time.sleep(10)

# --- Serial Communication ---
def send_command(dst, cmd, val=None):
    if serial_conn and serial_conn.is_open:
        payload = {"dst": dst, "cmd": cmd}
        if val is not None:
            payload["val"] = val
        serial_conn.write((json.dumps(payload) + "\n").encode())
        print(f"Sent Command: {payload}")

def serial_reader():
    global latest_telemetry
    while True:
        if serial_conn and serial_conn.is_open:
            try:
                line_bytes = serial_conn.readline()
                if not line_bytes:
                    continue
                
                try:
                    line = line_bytes.decode('utf-8', errors='ignore').strip()
                except Exception as e:
                    # Decoding completely failed, skip this line
                    continue

                if not line:
                    continue
                
                # Check if it looks like JSON
                if not (line.startswith('{') and line.endswith('}')):
                    # Likely a boot message or serial noise
                    if "Ready" in line or "gateway" in line:
                         print(f"Gateway Msg: {line}")
                    continue

                try:
                    data = json.loads(line)
                    if "type" in data and data["type"] == "telemetry":
                        src = data["src"]
                        latest_telemetry[src] = data["data"]
                        log_event(src, "telemetry", data["data"])
                    elif "event" in data:
                        print(f"Gateway Event: {data['event']}")
                except json.JSONDecodeError:
                    # Not valid JSON, ignore
                    pass
                    
            except Exception as e:
                print(f"Serial Error: {e}")
        time.sleep(0.01)

# --- Main Logic ---
def main():
    global serial_conn
    init_db()
    
    print("Smart Home Server Starting...")
    
    try:
        serial_conn = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    except Exception as e:
        print(f"Could not open serial port: {e}")
        # Continue anyway for debugging if asked, but usually fatal
    
    # Start Threads
    threading.Thread(target=check_presence, daemon=True).start()
    threading.Thread(target=serial_reader, daemon=True).start()
    
    # Interactive CLI
    print("\nCommands: stats, tare, led <on/off>, ir <code>, quit")
    try:
        while True:
            cmd_input = input("smart_home> ").strip().lower().split()
            if not cmd_input: continue
            
            cmd = cmd_input[0]
            
            if cmd == "quit": break
            elif cmd == "stats":
                print(f"Presence: {'HOME' if is_user_home else 'AWAY'}")
                print(f"Latest Data: {json.dumps(latest_telemetry, indent=2)}")
            elif cmd == "tare":
                send_command(1, "tare")
            elif cmd == "led":
                state = cmd_input[1] == "on"
                send_command(2, "set_state", {"on": state})
            elif cmd == "ir":
                code = cmd_input[1]
                send_command(3, "ir_send", code)
                
    except KeyboardInterrupt:
        pass
    finally:
        if serial_conn: serial_conn.close()
        print("Shutdown.")

if __name__ == "__main__":
    main()
