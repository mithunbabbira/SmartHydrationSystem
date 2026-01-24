import serial
import time
import json
import struct
import glob
import logging
import sys
import os
import threading
from datetime import datetime

# Add parent directory to path
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import server_config as config

def log(msg):
    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"[{timestamp}] GW: {msg}")

class GatewayService:
    def __init__(self, on_telemetry_callback=None):
        self.serial_conn = None
        self.gateway_verified = False
        self.on_telemetry_callback = on_telemetry_callback
        self.running = True
        self.read_thread = None

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
                    log(f"Attempting connection to {port}...")
                    self.serial_conn = serial.Serial(port, config.SERIAL_BAUD, timeout=0.1)
                    log(f"✓ Opened {port}. Waiting for Gateway Identity...")
                    self.gateway_verified = False
                    return # Connected
                except Exception as e:
                    log(f"Failed to open {port}: {e}")
                    continue
            
            time.sleep(2)

    def read_loop(self):
        """Continuously read from serial"""
        buffer = ""
        
        while self.running:
            if not self.serial_conn or not self.serial_conn.is_open:
                time.sleep(0.1)
                continue
                
            try:
                if self.serial_conn.in_waiting > 0:
                    chunk = self.serial_conn.read(self.serial_conn.in_waiting).decode('utf-8', errors='ignore')
                    buffer += chunk
                    
                    # Process complete lines
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if line:
                            self.handle_line(line)
                            
            except Exception as e:
                log(f"Read error: {e}")
                self.serial_conn = None
                time.sleep(1)

    def handle_line(self, line):
        """Handle a complete line from serial"""
        # Check for Gateway Identity
        if "MASTER_GATEWAY_READY" in line:
            if not self.gateway_verified:
                self.gateway_verified = True
                log("✓ VERIFIED: Gateway connected")
            return
        
        # Try to parse as JSON
        try:
            data = json.loads(line)
            log(f"← RX JSON: {json.dumps(data)}")
            
            msg_type = data.get("type")
            
            if msg_type == "telemetry":
                src = data.get("src", 0)
                payload = data.get("data", {})
                log(f"📊 Telemetry from Slave {src}: {payload}")
                
                if self.on_telemetry_callback:
                    self.on_telemetry_callback(src, payload)
                    
            elif msg_type == "response":
                log(f"📬 Response: {data}")
                
            elif msg_type == "query":
                src = data.get("src", 0)
                query_id = data.get("query_id", 0)
                log(f"❓ Query from Slave {src}: ID={query_id}")
                self.respond_to_query(src, query_id)
                
            else:
                log(f"⚠ Unknown message type: {msg_type}")
                
        except json.JSONDecodeError:
            # Not JSON, might be debug output
            if "Sent Hex" in line or "Received Hex" in line:
                log(f"Debug: {line}")
            # Silently ignore other non-JSON lines

    def respond_to_query(self, src, query_id):
        """Respond to query from slave"""
        if query_id == 1:  # TIME
            current_time = int(time.time())
            log(f"→ Responding to TIME query: {current_time}")
            self.send_command(src, "set_time", current_time)
        elif query_id == 2:  # PRESENCE
            # Import here to avoid circular dependency
            from services.presence import PresenceService
            # This is a hack - ideally we'd pass presence state via callback
            is_home = 1  # Default to home
            log(f"→ Responding to PRESENCE query: {is_home}")
            self.send_command(src, "set_presence", is_home)

    def send_command(self, slave_id, cmd_name, val):
        """Send command to slave via Gateway"""
        if not self.serial_conn or not self.serial_conn.is_open:
            log("⚠ Cannot send - not connected")
            return False
        
        # Map command names to IDs
        cmd_map = {
            "tare": 1,
            "snooze": 2,
            "alert": 3,
            "led": 4,
            "ir": 5,
            "set_time": 6,
            "set_presence": 7,
        }
        
        cmd_id = cmd_map.get(cmd_name)
        if cmd_id is None:
            log(f"⚠ Unknown command: {cmd_name}")
            return False
        
        # Pack command based on type
        if cmd_id in [1, 2, 3, 5, 6, 7]:  # Simple commands with int value
            # Header (3 bytes) + CMD_ID (1 byte) + VAL (4 bytes) = 8 bytes
            header_bytes = bytes([0x00, 0x02, slave_id])  # Magic=0x00, Type=0x02 (Command)
            cmd_byte = bytes([cmd_id])
            
            if isinstance(val, int):
                val_bytes = struct.pack('<I', val)  # Little-endian uint32
            else:
                val_bytes = struct.pack('<I', 0)
            
            packet = header_bytes + cmd_byte + val_bytes
            hex_str = packet.hex()
            
            log(f"→ TX [Slave {slave_id}] {cmd_name}({val}) => Hex: {hex_str}")
            
            try:
                self.serial_conn.write((hex_str + '\n').encode())
                return True
            except Exception as e:
                log(f"⚠ Send error: {e}")
                return False
                
        elif cmd_id == 4:  # LED (JSON payload)
            try:
                # For LED, val is dict
                header_bytes = bytes([0x00, 0x02, slave_id])
                cmd_byte = bytes([cmd_id])
                
                # Encode JSON to bytes, pack length, then data
                json_str = json.dumps(val) if isinstance(val, dict) else '{"cmd":"on"}'
                json_bytes = json_str.encode('utf-8')
                
                # For simplicity, pack as: header + cmd_id + json_bytes
                # Gateway will parse this specially
                packet = header_bytes + cmd_byte + json_bytes
                hex_str = packet.hex()
                
                log(f"→ TX [Slave {slave_id}] {cmd_name}({json_str}) => Hex: {hex_str}")
                
                self.serial_conn.write((hex_str + '\n').encode())
                return True
            except Exception as e:
                log(f"⚠ Send error: {e}")
                return False
        
        return False

    def start(self):
        """Start gateway service in background thread"""
        log("Starting Gateway Service...")
        self.connect()
        
        self.read_thread = threading.Thread(target=self.read_loop, daemon=True)
        self.read_thread.start()

    def stop(self):
        """Stop gateway service"""
        log("Stopping Gateway Service...")
        self.running = False
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
