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
import struct
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
    global gateway_verified
    if not gateway_verified:
        # print("✗ Command ignored: Gateway not verified yet.")
        return

    if serial_conn and serial_conn.is_open:
        try:
            # Check if this command should be packed into a binary struct
            if cmd in ["set_state", "tare", "snooze", "reset", "alert"]:
                payload = {"dst": dst}
                
                # Construct Binary Packet (Header + Payload)
                # Header: [SlaveID=0 (Master)][MsgType=2 (Command)][Version=1]
                # Struct format: <BBB (Little Endian)
                header_fmt = "<BBB" 
                header_vals = (0, 2, 1) # Source=0, Type=Command, Ver=1
                
                packet_bytes = bytearray()
                
                if cmd == "set_state":
                    # LEDData Struct: Header + bool, r, g, b, mode, speed
                    # Packed: <BBB ? BBB B B (Total 9 bytes)
                    # details: {'on': True, 'mode': 0, 'speed': 50, 'r': 255...}
                    details = val if isinstance(val, dict) else {}
                    fmt = header_fmt + "?BBBBB"
                    vals = header_vals + (
                        details.get("on", False),
                        details.get("r", 0),
                        details.get("g", 0),
                        details.get("b", 0),
                        details.get("mode", 0),
                        details.get("speed", 0)
                    )
                    packet_bytes = struct.pack(fmt, *vals)

                elif cmd in ["tare", "snooze", "reset", "alert"]:
                    # GenericCommand Struct: Header + cmd_id(B) + val(I - 4 bytes)
                    # Packed: <BBB B I (Total 8 bytes)
                    fmt = header_fmt + "BI"
                    
                    cmd_id = 0
                    val_int = 0 # Renamed to avoid conflict with function parameter 'val'
                    
                    if cmd == "tare": cmd_id = 1
                    elif cmd == "snooze": cmd_id = 2
                    elif cmd == "reset": cmd_id = 3
                    elif cmd == "alert": 
                        cmd_id = 3
                        val_int = val.get("level", 0) if isinstance(val, dict) else 0
                    
                    vals = header_vals + (cmd_id, val_int)
                    packet_bytes = struct.pack(fmt, *vals)
                    
                else:
                    print(f"Unknown command type for struct packing: {cmd}")
                    return

                # Send as Hex String
                # Master will decode this and forward blindly
                payload["raw"] = packet_bytes.hex()
                
                serial_conn.write((json.dumps(payload) + "\n").encode())
                print(f"Sent Hex: {payload['raw']}")
            else:
                # For other commands, send as regular JSON
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
    print("\nCommands: help, stats, tare, led <on/off/mode>, ir <code>, quit")
    try:
        while True:
            cmd_input = input("smart_home> ").strip().lower().split()
            if not cmd_input: continue
            
            cmd = cmd_input[0]
            
            if cmd == "quit": break
            elif cmd == "help":
                print("\n📚 Smart Home Server - Available Commands:")
                print("=" * 60)
                print("\n🏠 SYSTEM:")
                print("  help          - Show this help menu")
                print("  stats         - Display presence and telemetry data")
                print("  quit          - Exit the server")
                print("\n💧 HYDRATION MONITOR (Slave ID: 1):")
                print("  tare          - Zero the scale (remove bottle weight)")
                print("\n💡 LED STRIP (Slave ID: 2):")
                print("  led on        - Turn LED strip ON")
                print("  led off       - Turn LED strip OFF")
                print("  led red|green|blue|white|purple|yellow|cyan")
                print("                - Set static color")
                print("  led fade      - Smooth color fade effect")
                print("  led flash     - Fast flash effect")
                print("  led strobe    - Strobe effect")
                print("  led smooth    - Smooth transitions")
                print("  led rainbow   - Rainbow cycle effect")
                print("\n📡 IR TRANSMITTER (Slave ID: 3):")
                print("  ir <code>     - Send IR code (hex, e.g., 'ir FF6897')")
                print("\n" + "=" * 60 + "\n")
            elif cmd == "stats":
                print(f"Presence: {'HOME' if is_user_home else 'AWAY'}")
                print(f"Latest Data: {json.dumps(latest_telemetry, indent=2)}")
            elif cmd == "tare":
                send_command(1, "tare")
            elif cmd == "alert":
                if len(cmd_input) < 2:
                    print("Usage: alert <0/1/2> (0=Off, 1=Warning, 2=Critical)")
                    continue
                try:
                    level = int(cmd_input[1])
                    # Send Command ID 3 (Set Alert) to Slave 1
                    send_command(1, "alert", {"level": level})
                    print(f"Triggered Alert Level {level} on Hydration Monitor")
                except ValueError:
                    print("Invalid level")

            elif cmd == "led":
                if len(cmd_input) < 2:
                    print("Usage: led <on/off/red/green/blue/fade/flash/rainbow>")
                    print("       led mode <id> [speed]  (e.g., led mode 37 50)")
                    continue
                    
                action = cmd_input[1]
                
                # Raw Hex Command (e.g. led raw BB 25 32 44)
                if action == "raw":
                    if len(cmd_input) < 6:
                        print("Usage: led raw <b1> <b2> <b3> <b4> (Hex)")
                        continue
                    try:
                        b1 = int(cmd_input[2], 16)
                        b2 = int(cmd_input[3], 16)
                        b3 = int(cmd_input[4], 16)
                        b4 = int(cmd_input[5], 16)
                        # Pack into struct: mode=255, speed=b1, r=b2, g=b3, b=b4
                        send_command(2, "set_state", {
                            "on": True, "mode": 255, 
                            "speed": b1, "r": b2, "g": b3, "b": b4
                        })
                        print(f"Sending Bytes: {hex(b1)} {hex(b2)} {hex(b3)} {hex(b4)}")
                    except ValueError:
                        print("Invalid hex format")
                    continue
                
                # Raw Mode Command
                if action == "mode":
                    if len(cmd_input) < 3:
                        print("Usage: led mode <id> [speed]")
                        continue
                    try:
                        mode_id = int(cmd_input[2])
                        speed = int(cmd_input[3]) if len(cmd_input) > 3 else 50
                        send_command(2, "set_state", {"on": True, "mode": mode_id, "speed": speed})
                        print(f"Sending Mode {mode_id} (Speed {speed})")
                    except ValueError:
                        print("Invalid number format")
                    continue
                
                # LED mode mappings (common Triones modes)
                modes = {
                    "fade": 37,      # Seven color cross fade
                    "flash": 38,     # Red gradual change
                    "strobe": 48,    # Strobe
                    "smooth": 40,    # Cyan gradual change
                    "rainbow": 37,   # Seven color cross fade (alt)
                    "jump": 56,      # Seven color jump
                }
                
                # Color mappings
                colors = {
                    "red": (255, 0, 0),
                    "green": (0, 255, 0),
                    "blue": (0, 0, 255),
                    "white": (255, 255, 255),
                    "purple": (128, 0, 128),
                    "yellow": (255, 255, 0),
                    "cyan": (0, 255, 255),
                }
                
                if action == "on":
                    send_command(2, "set_state", {"on": True})
                elif action == "off":
                    send_command(2, "set_state", {"on": False})
                elif action in modes:
                    send_command(2, "set_state", {"on": True, "mode": modes[action], "speed": 50})
                elif action in colors:
                    r, g, b = colors[action]
                    send_command(2, "set_state", {"on": True, "mode": 0, "r": r, "g": g, "b": b})
                else:
                    print(f"Unknown LED action: {action}")
                    
            elif cmd == "ir":
                if len(cmd_input) < 2:
                    print("Usage: ir <code>  (e.g., ir FF6897)")
                    continue
                code = cmd_input[1]
                send_command(3, "ir_send", code)
                
    except KeyboardInterrupt:
        pass
    finally:
        if serial_conn: serial_conn.close()
        print("Shutdown.")

if __name__ == "__main__":
    main()
