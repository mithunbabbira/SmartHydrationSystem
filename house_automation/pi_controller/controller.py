import serial
import time
import threading
import queue
import logging
import sys
import re
import struct
import config
import subprocess

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
            # Reset ESP32 via DTR (Optional, but good for clean start)
            self.serial_conn.dtr = False 
            time.sleep(0.1)
            self.serial_conn.dtr = True
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
                        self.watchdog.pet() # Reset watchdog on ANY valid serial data
                        self.process_incoming_data(line)
            except Exception as e:
                logger.error(f"Error reading from serial: {e}")
                time.sleep(1) 

    def is_phone_home(self):
        try:
            # Check Bluetooth Presence
            # Note: Requires hcitool installed (sudo apt-get install bluez)
            # PHONE_MAC like "F0:24:F9:0D:90:A4"
            phone_mac = config.SLAVE_MACS.get('my_phone', "F0:24:F9:0D:90:A4") 
            result = subprocess.check_output(["hcitool", "name", phone_mac], stderr=subprocess.STDOUT)
            return bool(result.strip())
        except Exception:
            return False

    def process_incoming_data(self, line):
        # ... (Heartbeat check) ...

        match = re.search(r'RX:([0-9A-Fa-f:]+):([0-9A-Fa-f]+)', line)
        if match:
            mac = match.group(1)
            hex_data = match.group(2)
            try:
                data_bytes = bytes.fromhex(hex_data)
                
                # Check for Hydration or Protocol Commands (6 bytes)
                if len(data_bytes) == 6:
                    ctype, cmd, val = struct.unpack('<BBf', data_bytes)
                    
                    # 0x21: REPORT_WEIGHT
                    if cmd == 0x21:
                        logger.info(f"HYDRATION WEIGHT: {val:.2f} g")
                    
                    # 0x30: REQUEST_TIME from Slave
                    elif cmd == 0x30:
                        logger.info(f"[{mac}] Requested Time.")
                        unix_time = int(time.time())
                        # Pack uint32 as float bytes or just send raw uint32?
                        # Our struct has 'float val'. 
                        # We need to pack uint32 into those 4 bytes.
                        # Python: struct.pack('<I', unix_time).hex()
                        time_hex = struct.pack('<I', unix_time).hex()
                        # Resp: 01 31 <TIME_HEX>
                        self.send_command(mac, "0131" + time_hex)

                    # 0x40: REQUEST_PRESENCE from Slave
                    elif cmd == 0x40:
                        logger.info(f"[{mac}] Requested Presence Check.")
                        is_home = self.is_phone_home()
                        # Resp: 01 41 <1.0 or 0.0>
                        # Use float 1.0/0.0 for simplicity with existing float val
                        payload = "0000803F" if is_home else "00000000" # 1.0 or 0.0 in float hex
                        self.send_command(mac, "0141" + payload)

                    # 0x50: ALERT_MISSING
                    elif cmd == 0x50:
                        logger.warning(f"ALERT [{mac}]: Bottle Missing! (Timer Expired)")
                    
                    # 0x51: ALERT_REPLACED
                    elif cmd == 0x51:
                         logger.info(f"ALERT [{mac}]: Bottle Replaced. Stabilizing...")

                    # 0x60: DRINK_DETECTED
                    elif cmd == 0x60:
                        logger.info(f"HYDRATION [{mac}]: Drink Detected: {val:.2f} ml")

                    # 0x61: DAILY_TOTAL
                    elif cmd == 0x61:
                        logger.info(f"HYDRATION [{mac}]: Daily Total: {val:.2f} ml")
                    
                    else:
                        logger.info(f"SENSOR [{mac}] -> Type:{ctype} Cmd:0x{cmd:02X} Val:{val:.2f}")
                else:
                    logger.info(f"DATA [{mac}] -> RAW HEX: {hex_data}")
            except Exception as e:
                logger.error(f"Failed to decode data from {mac}: {e}")

    def send_command(self, mac_address, hex_data):
        # Format: TX:<MAC>:<HEX_DATA>
        command = f"TX:{mac_address}:{hex_data}\n"
        try:
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.write(command.encode('utf-8'))
                logger.info(f"SENT to {mac_address}: {hex_data}")
            else:
                logger.error("Serial connection lost. Cannot send.")
        except Exception as e:
            logger.error(f"Error sending data: {e}")

    def start(self):
        if not self.connect():
            return

        self.running = True
        
        # Start Watchdog
        self.watchdog = WatchdogThread(self.serial_conn)
        self.watchdog.start()

        # Start Reader
        self.read_thread = threading.Thread(target=self.reader_thread)
        self.read_thread.daemon = True
        self.read_thread.start()
        
        self.ui_loop()

    def ui_loop(self):
        print("\n--- House Automation Controller (Transparent Mode) ---")
        print("Format: <MAC_ADDRESS> <HEX_MESSAGE>")
        print("Example: 24:6F:28:A1:B2:C3 0102aabbcc")
        print("Type 'exit' to quit.\n")
        
        while self.running:
            try:
                user_input = input("Enter command: ").strip()
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
                            # 0x10 = SET_LED. Payload: Type(1) Cmd(0x10) Val(float)
                            # Hex: 01 10 0000803F (1.0) or 00000000
                            hex_payload = "0110" + ("0000803F" if val else "00000000")
                            self.send_command(mac, hex_payload)

                        elif subcmd == 'buzzer':
                            val = 1 if (len(parts)>2 and parts[2]=='on') else 0
                            # 0x11 = SET_BUZZER
                            hex_payload = "0111" + ("0000803F" if val else "00000000")
                            self.send_command(mac, hex_payload)

                        elif subcmd == 'rgb':
                            # Valid codes: 0-4
                            if len(parts) > 2:
                                code = int(parts[2])
                                # 0x12 = SET_RGB. Float representation of int code.
                                float_hex = struct.pack('<f', code).hex()
                                hex_payload = "0112" + float_hex
                                self.send_command(mac, hex_payload)
                            else:
                                logger.warning("Usage: hydration rgb <0-4>")

                        elif subcmd == 'weight':
                            # 0x20 = GET_WEIGHT. 
                            self.send_command(mac, "012000000000")

                        elif subcmd == 'tare':
                            # 0x22 = CMD_TARE
                            self.send_command(mac, "012200000000")
                            logger.info("Sent TARE command.")

                        else:
                            logger.warning("Unknown hydration command.")

                    except KeyError:
                         logger.error("Hydration MAC not found in config.SLAVE_MACS")
                    except Exception as e:
                         logger.error(f"Command Error: {e}")

                # --- Raw Hex Fallback ---
                elif len(parts) == 2:
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
