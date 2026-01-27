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

# Import Handlers
from handlers.hydration import HydrationHandler
from handlers.led import LEDHandler
from handlers.ir import IRHandler

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
        
        # Initialize Handlers
        self.handlers = {
            'hydration': HydrationHandler(self),
            'led': LEDHandler(self)
        }

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
        # PHONE_MAC like "F0:24:F9:0D:90:A4"
        phone_mac = config.SLAVE_MACS.get('my_phone', "00:00:00:00:00:00") 
        logger.info(f"Checking presence for {phone_mac}...")

        # Method 1: l2ping (Preferred, needs sudo usually)
        try:
            # -c 1: count 1, -t 2: timeout 2s
            subprocess.check_output(["sudo", "l2ping", "-c", "1", "-t", "2", phone_mac], stderr=subprocess.STDOUT)
            logger.info(f"Presence Confirmed via l2ping.")
            return True
        except subprocess.CalledProcessError:
            pass # l2ping failed, try next method
        except Exception as e:
             logger.error(f"l2ping error: {e}")

        # Method 2: hcitool name (Fallback)
        try:
            result = subprocess.check_output(["hcitool", "name", phone_mac], stderr=subprocess.STDOUT)
            output = result.strip().decode('utf-8')
            if output:
                logger.info(f"Presence Confirmed via hcitool name: '{output}'")
                return True
        except Exception as e:
            logger.error(f"hcitool error: {e}")
        
        logger.info("Presence Check Failed (User AWAY)")
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
                    
                    # Route by Type
                    if ctype == 1: # Hydration
                        self.handlers['hydration'].handle_packet(cmd, val, mac)
                    elif ctype == 2: # LED (Future)
                        self.handlers['led'].handle_packet(cmd, val, mac)
                    else:
                        logger.info(f"UNKNOWN TYPE [{mac}] -> Type:{ctype} Cmd:0x{cmd:02X} Val:{val:.2f}")
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
        print("\n--- House Automation Controller (Modular) ---")
        print("Commands: hydration <cmd>, led <cmd>")
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
                
                # Route User Input
                if cmd in self.handlers:
                    self.handlers[cmd].handle_user_input(parts)
                
                # --- Raw Hex Fallback ---
                elif len(parts) == 2:
                    mac = parts[0]
                    hex_val = parts[1].strip().replace('0x', '')
                    # Basic validation
                    if len(mac) == 17 and mac.count(':') == 5:
                        if all(c in '0123456789ABCDEFabcdef' for c in hex_val) and len(hex_val) % 2 == 0:
                             self.send_command(mac, hex_val)
                        else:
                             logger.warning("Please provide a valid even-length HEX payload (e.g. 0102AABBCC)")
                    else:
                        logger.warning("Invalid MAC format. Use XX:XX:XX:XX:XX:XX")
                else:
                    logger.warning("Invalid Input. Format: <handler> <cmd> or <MAC> <HEX>")
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
