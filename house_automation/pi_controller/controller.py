import serial
import time
import threading
import queue
import logging
import sys
import re
import struct
import config

# --- Configuration ---
SERIAL_PORT = config.SERIAL_PORT
BAUD_RATE = 115200

# --- Logging Setup ---
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.StreamHandler(sys.stdout)
    ]
)
logger = logging.getLogger("PiController")

class SerialController:
    def __init__(self, port, baud_rate):
        self.port = port
        self.baud_rate = baud_rate
        self.serial_conn = None
        self.running = False
        self.send_queue = queue.Queue()

    def connect(self):
        try:
            self.serial_conn = serial.Serial(self.port, self.baud_rate, timeout=1)
            logger.info(f"Connected to {self.port} at {self.baud_rate} baud.")
            return True
        except serial.SerialException as e:
            logger.error(f"Failed to connect to serial port: {e}")
            return False

    def reader_thread(self):
        logger.info("Reader thread started.")
        while self.running and self.serial_conn and self.serial_conn.is_open:
            try:
                if self.serial_conn.in_waiting > 0:
                    line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        # logger.debug(f"RAW SERIAL: {line}")
                        self.watchdog.pet() # Reset watchdog on ANY valid serial data
                        self.process_incoming_data(line)
            except Exception as e:
                logger.error(f"Error reading from serial: {e}")
                time.sleep(1) 

    def process_incoming_data(self, line):
        # 1. Heartbeat
        if line == "HEARTBEAT":
            return

        # 2. Sensor Data: RX:<MAC>:<HEX_DATA>
        match = re.search(r'RX:([0-9A-Fa-f:]+):([0-9A-Fa-f]+)', line)
        if match:
            mac = match.group(1)
            hex_data = match.group(2)
            try:
                # Decode Hex to Bytes
                data_bytes = bytes.fromhex(hex_data)
                
                # Unpack Struct: <BBI (Type, Command, Data/Value)
                if len(data_bytes) == 6:
                    ctype, cmd, val_int = struct.unpack('<BBI', data_bytes)
                    
                    # 0x21: WEIGHT REPORT (val_int is float bits)
                    if ctype == 1 and cmd == 0x21:
                        # Re-interpret bits as float
                        val_float = struct.unpack('<f', struct.pack('<I', val_int))[0]
                        logger.info(f"HYDRATION WEIGHT: {val_float:.2f} g")
                    
                    # 0x30: GET TIME REQUEST
                    elif cmd == 0x30:
                        logger.info(f"Slave {mac} requested TIME.")
                        self.send_time_sync(mac)
                        
                    else:
                        logger.info(f"SENSOR [{mac}] -> Type:{ctype} Cmd:0x{cmd:02X} Data (Int):{val_int}")
                else:
                    logger.info(f"DATA [{mac}] -> RAW HEX: {hex_data}")
            except Exception as e:
                logger.error(f"Failed to decode data from {mac}: {e}")
            
        elif line.startswith("DEBUG:"):
            logger.debug(f"Master: {line}")
        elif line.startswith("OK:") or line.startswith("ERR:"):
            logger.info(f"Master: {line}")

    def send_time_sync(self, mac_address):
        # Send 0x31 (SET_TIME) with current Unix Timestamp
        timestamp = int(time.time())
        # Payload: Type(1) Cmd(0x31) Data(timestamp)
        # Hex
        payload = struct.pack('<BBI', 1, 0x31, timestamp).hex()
        # Wait, Master expects raw hex payload bytes? Yes.
        # But wait, Master sends it as bytes.
        # My hex string must represent Type+Cmd+Data.
        # So "0131" + timestamp_hex.
        self.send_command(mac_address, payload)

    def ui_loop(self):
        print("\n--- House Automation Controller (Transparent Mode) ---")
        # ... (Help text omitted for brevity) ...
        
        while self.running:
            try:
                user_input = input("Enter command: ").strip()
                # ... (Exit logic) ...
                if user_input.lower() == 'exit':
                    logger.info("Exiting...")
                    self.running = False
                    self.watchdog.stop()
                    break

                parts = user_input.split(' ')
                cmd = parts[0].lower()
                
                # --- Hydration Shortcuts ---
                if cmd == 'hydration':
                    try:
                        mac = config.SLAVE_MACS['hydration']
                        subcmd = parts[1].lower() if len(parts) > 1 else ''
                        
                        if subcmd == 'led':
                            val = 1 if (len(parts)>2 and parts[2]=='on') else 0
                            # 0x10. Data is INT 0 or 1.
                            self.send_command(mac, f"0110{val:08X}")

                        elif subcmd == 'buzzer':
                            val = 1 if (len(parts)>2 and parts[2]=='on') else 0
                            self.send_command(mac, f"0111{val:08X}")

                        elif subcmd == 'rgb':
                            if len(parts) > 2:
                                code = int(parts[2])
                                self.send_command(mac, f"0112{code:08X}")
                            else:
                                logger.warning("Usage: hydration rgb <0-4>")

                        elif subcmd == 'weight':
                            self.send_command(mac, "012000000000")
                            
                        elif subcmd == 'time':
                             self.send_time_sync(mac)

                        else:
                            logger.warning("Unknown hydration command.")

                    except KeyError:
                         logger.error("Hydration MAC not found.")
                    except Exception as e:
                         logger.error(f"Command Error: {e}")
                
                 # ... (Raw Hex Fallback) ...
                elif len(parts) == 2:
# ...
                    mac = parts[0]
                    hex_val = parts[1].strip().replace('0x', '')
                    # Basic validation
                    if len(mac) == 17 and mac.count(':') == 5:
                        # Ensure hex_val is valid hex
                        if all(c in '0123456789ABCDEFabcdef' for c in hex_val) and len(hex_val) % 2 == 0:
                             self.send_command(mac, hex_val)
                        else:
                             logger.warning("Please provide a valid even-length HEX payload (e.g. 0102AABBCC)")
                    else:
                        logger.warning("Invalid MAC format. Use XX:XX:XX:XX:XX:XX")
                else:
                    logger.warning("Invalid Input. Format: <MAC> <HEX_MSG> or hydration cmd")
            except KeyboardInterrupt:
                self.running = False
                self.watchdog.stop()
                break
            except Exception as e:
                logger.error(f"Input Error: {e}")

        if self.serial_conn:
            self.serial_conn.close()

class WatchdogThread(threading.Thread):
    def __init__(self, serial_conn, timeout=60):
        super().__init__()
        self.serial_conn = serial_conn
        self.timeout = timeout
        self.last_pet = time.time()
        self.running = True
        self.daemon = True

    def pet(self):
        self.last_pet = time.time()

    def stop(self):
        self.running = False

    def run(self):
        logger.info("Watchdog started.")
        while self.running:
            time.sleep(1)
            if time.time() - self.last_pet > self.timeout:
                logger.critical(f"WATCHDOG TRIGGERED! Last output was {time.time() - self.last_pet:.1f}s ago. Resetting Master via DTR...")
                self.reset_master()
                self.pet() # Reset timer to avoid loop while resetting

    def reset_master(self):
        try:
            # Toggle DTR to reset ESP32
            self.serial_conn.dtr = False
            time.sleep(0.1)
            self.serial_conn.dtr = True
            time.sleep(0.1)
            self.serial_conn.dtr = False
            logger.info("Master Reset Signal Sent.")
        except Exception as e:
            logger.error(f"Failed to reset Master: {e}")

if __name__ == "__main__":
    # Allow passing port as argument
    port = SERIAL_PORT
    if len(sys.argv) > 1:
        port = sys.argv[1]
    
    controller = SerialController(port, BAUD_RATE)
    controller.start()
