#!/usr/bin/env python3
"""
Smart Home Central Server - Mac Test Version
===============================================
Simplified version for local Mac testing without Bluetooth presence.
"""

import serial
import json
import time
import threading
import serial.tools.list_ports
import struct
import glob

# --- Configuration ---
BAUD_RATE = 115200

# --- Globals ---
latest_telemetry = {}
serial_conn = None
gateway_verified = False

def find_serial_port():
    """Auto-detect potential ESP32 serial ports on Mac"""
    patterns = ['/dev/cu.usbserial*', '/dev/cu.SLAB*', '/dev/cu.wchusbserial*', '/dev/tty.usbserial*']
    ports = []
    for pattern in patterns:
        ports.extend(glob.glob(pattern))
    return ports

# --- Serial Communication ---
def connect_serial():
    global serial_conn, gateway_verified
    
    while True:
        if serial_conn and serial_conn.is_open:
            time.sleep(1)
            continue
            
        ports = find_serial_port()
        if not ports:
            print("⏳ Searching for Master Gateway...")
            time.sleep(2)
            continue
            
        for port in ports:
            try:
                print(f"Attempting connection to {port}...")
                serial_conn = serial.Serial(port, BAUD_RATE, timeout=1)
                time.sleep(2)  # Allow ESP32 to boot if just connected
                print(f"✓ Opened {port}. Waiting for Gateway Identity...")
                gateway_verified = False
                break
            except Exception as e:
                print(f"✗ Failed to open {port}: {e}")
                continue
        
        time.sleep(1)

def send_command(dst, cmd, val=None):
    global serial_conn, gateway_verified
    
    if not gateway_verified:
        print("⚠ Gateway not verified yet. Command ignored.")
        return
        
    if not serial_conn or not serial_conn.is_open:
        print("⚠ No serial connection")
        return
    try:
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
            val = 0
            
            if cmd == "tare": cmd_id = 1
            elif cmd == "snooze": cmd_id = 2
            elif cmd == "reset": cmd_id = 3
            elif cmd == "alert": 
                cmd_id = 3
                val = details.get("level", 0)
            
            vals = header_vals + (cmd_id, val)
            packet_bytes = struct.pack(fmt, *vals)
            
        else:
            print(f"Unknown command type for struct packing: {cmd}")
            return

        # Send as Hex String
        # Master will decode this and forward blindly
        payload["raw"] = packet_bytes.hex()
        
        if serial_conn and serial_conn.is_open:
            serial_conn.write((json.dumps(payload) + "\n").encode())
            print(f"Sent Hex: {payload['raw']}")
            
    except Exception as e:
        print(f"Send failed: {e}")

def serial_reader():
    global serial_conn, gateway_verified, latest_telemetry
    
    while True:
        if serial_conn and serial_conn.is_open:
            try:
                line = serial_conn.readline().decode('utf-8', errors='ignore').strip()
                if not line:
                    continue
                    
                # Try parsing as JSON
                try:
                    data = json.loads(line)
                    
                    # Check for Gateway Identity
                    if "type" in data and data["type"] == "gateway_id":
                        if not gateway_verified:
                            gateway_verified = True
                            print(f"✅ VERIFIED: Smart Home Master Gateway connected")
                    
                    # Handle telemetry
                    elif "type" in data and data["type"] == "telemetry":
                        src = data.get("src", "unknown")
                        telemetry_data = data.get("data", {})
                        latest_telemetry[src] = telemetry_data
                        print(f"📊 Telemetry from Slave {src}: {telemetry_data}")
                    
                    # Handle events
                    elif "event" in data:
                        print(f"📢 Gateway Event: {data}")
                        
                except json.JSONDecodeError:
                    # Not JSON - probably boot messages or debug output
                    print(f"🔍 Gateway Log: {line}")
                    pass
                    
            except Exception as e:
                print(f"Serial Read Error: {e}")
                if serial_conn:
                    serial_conn.close()
                gateway_verified = False
                time.sleep(1)
                
        time.sleep(0.01)

# --- Main Logic ---
def main():
    
    print("🏠 Smart Home Server (Mac Test Mode)")
    print("=" * 60)
    print("Searching for Master Gateway on USB/Serial...\n")
    
    # Start Threads
    threading.Thread(target=connect_serial, daemon=True).start()
    threading.Thread(target=serial_reader, daemon=True).start()
    
    # Interactive CLI
    print("\n📚 Commands: help, stats, led <on/off/mode>, ir <code>, tare, quit")
    print("=" * 60 + "\n")
    
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
                print("  stats         - Display telemetry data")
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
                print(f"\n📊 Latest Telemetry:")
                if latest_telemetry:
                    print(json.dumps(latest_telemetry, indent=2))
                else:
                    print("  No data received yet")
                print()
            elif cmd == "tare":
                send_command(1, "tare")
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
        print("\n👋 Shutdown.")

if __name__ == "__main__":
    main()
