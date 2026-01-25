import serial
import time
import threading
import queue
import logging
import sys
import re
import struct

# --- Configuration ---
SERIAL_PORT = '/dev/ttyUSB0' # DEFAULT - Change if needed
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
            # logger.debug("Heartbeat received.")
            return

        # 2. Sensor Data: RX:<MAC>:<HEX_DATA>
        match = re.search(r'RX:([0-9A-Fa-f:]+):([0-9A-Fa-f]+)', line)
        if match:
            mac = match.group(1)
            hex_data = match.group(2)
            try:
                # Decode Hex to Bytes
                data_bytes = bytes.fromhex(hex_data)
                
                # Unpack Struct: <BBfI (Type, Command, Value, Battery)
                if len(data_bytes) == 10:
                    ctype, cmd, val, bat = struct.unpack('<BBfI', data_bytes)
                    logger.info(f"SENSOR [{mac}] -> Type:{ctype} Cmd:{cmd} Val:{val:.2f} Bat:{bat}mV")
                else:
                    logger.warning(f"Invalid Data Length from {mac}: {len(data_bytes)} bytes")
            except Exception as e:
                logger.error(f"Failed to decode data from {mac}: {e}")
            
        elif line.startswith("RX:"):
             logger.warning(f"Malformed RX line: {line}")
        elif line.startswith("DEBUG:") or line.startswith("OK:"):
            logger.debug(f"Master: {line}")

    def send_command(self, mac_address, data):
        # Format: TX:<MAC>:<Data>
        command = f"TX:{mac_address}:{data}\n"
        try:
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.write(command.encode('utf-8'))
                logger.info(f"SENT to {mac_address}: {data}")
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
        print("\n--- House Automation Controller ---")
        print("Format: <MAC_ADDRESS> <MESSAGE>")
        print("Example: 24:6F:28:A1:B2:C3 TurnOnLight")
        print("Type 'exit' to quit.\n")
        
        while self.running:
            try:
                user_input = input("Enter command: ").strip()
                if user_input.lower() == 'exit':
                    logger.info("Exiting...")
                    self.running = False
                    self.watchdog.stop()
                    break
                
                parts = user_input.split(' ', 1)
                if len(parts) == 2:
                    mac = parts[0]
                    msg = parts[1]
                    if len(mac) == 17 and mac.count(':') == 5:
                        self.send_command(mac, msg)
                    else:
                        logger.warning("Invalid MAC format. Use XX:XX:XX:XX:XX:XX")
                else:
                    logger.warning("Invalid Input. Format: <MAC> <MSG>")
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
