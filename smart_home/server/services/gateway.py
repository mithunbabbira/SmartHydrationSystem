import serial
import time
import json
import struct
import glob
import logging
import sys
import os

# Add parent directory to path to find server_config if needed
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import server_config as config

class GatewayService:
    def __init__(self, on_telemetry_callback=None):
        self.serial_conn = None
        self.gateway_verified = False
        self.on_telemetry_callback = on_telemetry_callback
        self.running = True

    def find_serial_port(self):
        """Auto-detect potential ESP32 serial ports"""
        if sys.platform.startswith('darwin'):
            ports = glob.glob('/dev/tty.usbserial*') + glob.glob('/dev/tty.SLAB_USBtoUART*') + glob.glob('/dev/cu.usbserial*') + glob.glob('/dev/tty.usbmodem*')
        elif sys.platform.startswith('linux'):
            ports = glob.glob('/dev/ttyUSB*') + glob.glob('/dev/ttyACM*') + glob.glob('/dev/serial/by-id/*')
        else:
            ports = []
        return ports

    def connect(self):
        """Main connection loop (blocking or threaded)"""
        while self.running:
            if self.serial_conn and self.serial_conn.is_open:
                time.sleep(1)
                continue
                
            ports = self.find_serial_port()
            if not ports:
                time.sleep(2)
                continue
                
            for port in ports:
                try:
                    print(f"Attempting connection to {port}...")
                    self.serial_conn = serial.Serial(port, config.SERIAL_BAUD, timeout=0.1)
                    print(f"✓ Opened {port}. Waiting for Gateway Identity...")
                    self.gateway_verified = False
                    return # Connected
                except Exception as e:
                    pass
            time.sleep(2)

    def start_reader(self):
        """Read loop"""
        while self.running:
            if not self.serial_conn or not self.serial_conn.is_open:
                self.connect()
                
            try:
                line_bytes = self.serial_conn.readline()
                if not line_bytes: continue
                
                try:
                    line = line_bytes.decode('utf-8', errors='ignore').strip()
                except Exception: continue

                if not line: continue
                
                # Filter Logs
                if not (line.startswith('{') and line.endswith('}')):
                    if any(x in line for x in ["Ready", "gateway", "boot", "rst:"]):
                        print(f"Gateway Log: {line}")
                    continue

                try:
                    data = json.loads(line)
                    if "type" in data:
                        m_type = data["type"]
                        if m_type == "telemetry":
                            src = data["src"]
                            
                            # Parse Raw
                            telemetry = self.parse_generic_telemetry(data)
                            if telemetry and self.on_telemetry_callback:
                                self.on_telemetry_callback(src, telemetry)

                        elif m_type == "gateway_id":
                            if not self.gateway_verified:
                                print(f"✓ VERIFIED: Gateway connected on {self.serial_conn.port}")
                                self.gateway_verified = True
                                
                    elif "event" in data:
                        print(f"Gateway Event: {data['event']}")
                except json.JSONDecodeError:
                    pass
                    
            except Exception as e:
                print(f"Serial Error: {e}")
                if self.serial_conn: self.serial_conn.close()
                self.gateway_verified = False
                time.sleep(1)

    def parse_generic_telemetry(self, data):
        """Parse raw hex telemetry from Generic Gateway"""
        src = data.get("src")
        if "raw" in data:
            raw_hex = data["raw"]
            try:
                raw_bytes = bytes.fromhex(raw_hex)
                if len(raw_bytes) < 3: return None
                
                if src == 1: # Hydration
                    # <BBB ff B ?
                    if len(raw_bytes) >= 13:
                        unpacked = struct.unpack("<BBBffB?", raw_bytes[:13])
                        return {
                            "weight": round(unpacked[3], 2),
                            "delta": round(unpacked[4], 2),
                            "alert": unpacked[5],
                            "missing": unpacked[6]
                        }
                elif src == 2: # LED
                    if len(raw_bytes) >= 9:
                        unpacked = struct.unpack("<BBB?BBBBB", raw_bytes[:9])
                        return {
                            "is_on": unpacked[3],
                            "r": unpacked[4], "g": unpacked[5], "b": unpacked[6],
                            "mode": unpacked[7], "speed": unpacked[8]
                        }
            except:
                return None
        elif "weight" in data: # Legacy Fallback
             return data
        return None

    def send_command(self, dst, cmd, val=None):
        if not self.gateway_verified: return

        try:
            # Generic binary packing logic
            payload = {"dst": dst}
            raw_hex = ""
            
            # Header: [ver][type][src][dst][cmd]
            # Ver=1, Type=2(CMD), Src=0(Master), Dst=?, Cmd=?
            header_fmt = "<BBBBB"
            cmd_id = 0
            
            # Pack payload based on command type (Hydration, Leo, IR)
            # ... (Simplified generic packing logic from main script) ...
            # Reuse logic:
            packet_bytes = b''
            
            if cmd == "led":
                # LED Logic
                details = val if isinstance(val, dict) else {}
                header_vals = (1, 2, 0, dst, 1) # Cmd ID 1 = LED
                fmt = header_fmt + "?BBBBB"
                vals = header_vals + (
                    details.get("on", False),
                    details.get("r", 0), details.get("g", 0), details.get("b", 0),
                    details.get("mode", 0), details.get("speed", 0)
                )
                packet_bytes = struct.pack(fmt, *vals)
            
            elif cmd in ["tare", "snooze", "reset", "alert"]:
                header_vals = (1, 2, 0, dst, 2) # Cmd ID 2 = Generic
                fmt = header_fmt + "BI"
                
                c_id = 0
                v_int = 0
                if cmd == "tare": c_id = 1
                elif cmd == "snooze": c_id = 2
                elif cmd == "reset": c_id = 3
                elif cmd == "alert": 
                    c_id = 3
                    v_int = val.get("level", 0) if isinstance(val, dict) else 0

                vals = header_vals + (c_id, v_int)
                packet_bytes = struct.pack(fmt, *vals)

            if packet_bytes:
                raw_hex = packet_bytes.hex()
                payload["raw"] = raw_hex
                print(f"Sent Hex: {raw_hex}") # Debug enabled
                self.serial_conn.write((json.dumps(payload) + "\n").encode())

        except Exception as e:
            print(f"Send Failed: {e}")

    def stop(self):
        self.running = False
        if self.serial_conn: self.serial_conn.close()
