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
import os
from datetime import datetime
from datetime import datetime

import glob

# --- Configuration ---
BAUD_RATE = 115200
PHONE_MAC = "48:EF:1C:49:6A:E7" # User's device
PHONE_MAC = "48:EF:1C:49:6A:E7" # User's device

# --- Globals ---
is_user_home = False
latest_telemetry = {}
serial_conn = None
gateway_verified = False

def find_serial_port():
    """Auto-detect potential ESP32 serial ports"""
    # Common Raspberry Pi serial ports for ESP32/USB-Serial
    patterns = ['/dev/ttyUSB*', '/dev/ttyACM*', '/dev/serial/by-id/*']
    ports = []
    for pattern in patterns:
        ports.extend(glob.glob(pattern))
    
    # Filter out common non-ESP32 ports if necessary, but usually USB* is correct
    return ports

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
def connect_serial():
    global serial_conn, gateway_verified
    
    while True:
        if serial_conn and serial_conn.is_open:
            time.sleep(1)
            continue
            
        ports = find_serial_port()
        if not ports:
            # print("searching for gateway...")
            time.sleep(2)
            continue
            
        for port in ports:
            try:
                print(f"Attempting connection to {port}...")
                serial_conn = serial.Serial(port, BAUD_RATE, timeout=0.1)
                print(f"✓ Opened {port}. Waiting for Gateway Identity...")
                gateway_verified = False
                return # Successfully opened
            except Exception as e:
                # print(f"Could not open {port}: {e}")
                pass
        
        time.sleep(2)

def send_command(dst, cmd, val=None):
    if not gateway_verified:
        # print("✗ Command ignored: Gateway not verified yet.")
        return

    if serial_conn and serial_conn.is_open:
        try:
            payload = {"dst": dst, "cmd": cmd}
            if val is not None:
                payload["val"] = val
            serial_conn.write((json.dumps(payload) + "\n").encode())
            print(f"Sent Command: {payload}")
        except Exception as e:
            print(f"Send failed: {e}")
            gateway_verified = False

def serial_reader():
    global latest_telemetry, gateway_verified, serial_conn
    while True:
        if not serial_conn or not serial_conn.is_open:
            connect_serial()
            
        try:
            line_bytes = serial_conn.readline()
            if not line_bytes:
                continue
            
            try:
                line = line_bytes.decode('utf-8', errors='ignore').strip()
            except Exception as e:
                continue

            if not line:
                continue
            
            # Check if it looks like JSON
            if not (line.startswith('{') and line.endswith('}')):
                # Filter out obvious boot/reboot messages
                if any(x in line for x in ["Ready", "gateway", "boot", "rst:"]):
                     print(f"Gateway Log: {line}")
                continue

            try:
                data = json.loads(line)
                if "type" in data:
                    m_type = data["type"]
                    if m_type == "telemetry":
                        src = data["src"]
                        latest_telemetry[src] = data["data"]
                    elif m_type == "gateway_id":
                        if not gateway_verified:
                            print(f"✓ VERIFIED: Smart Home Master Gateway connected on {serial_conn.port}")
                            gateway_verified = True
                    
                elif "event" in data:
                    print(f"Gateway Event: {data['event']}")
            except json.JSONDecodeError:
                pass
                
        except Exception as e:
            print(f"Serial Error: {e}")
            if serial_conn:
                serial_conn.close()
            gateway_verified = False
            time.sleep(1)
            
        time.sleep(0.01)

# --- Main Logic ---
def main():
    
    print("Smart Home Server Starting...")
    print("Searching for Master Gateway on USB/Serial...")
    
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
