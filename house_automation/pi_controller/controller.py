import serial
import time
import threading
import queue
import logging
import sys
import re

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
                        logger.debug(f"RAW SERIAL: {line}")
                        self.process_incoming_data(line)
            except Exception as e:
                logger.error(f"Error reading from serial: {e}")
                time.sleep(1) 

    def process_incoming_data(self, line):
        # Expected Format: RX:<MAC>:<Data>
        match = re.search(r'RX:([0-9A-Fa-f:]+):(.*)', line)
        if match:
            mac = match.group(1)
            data = match.group(2)
            logger.info(f"RECEIVED from {mac}: {data}")
        elif line.startswith("RX:"):
             logger.warning(f"Malformed RX line: {line}")
        else:
            # Maybe a debug message from Master
            logger.debug(f"Master Log: {line}")

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
                    break
                
                parts = user_input.split(' ', 1)
                if len(parts) == 2:
                    mac = parts[0]
                    msg = parts[1]
                    # Basic MAC validation
                    if len(mac) == 17 and mac.count(':') == 5:
                        self.send_command(mac, msg)
                    else:
                        logger.warning("Invalid MAC format. Use XX:XX:XX:XX:XX:XX")
                else:
                    logger.warning("Invalid Input. Format: <MAC> <MSG>")
            except KeyboardInterrupt:
                self.running = False
                break
            except Exception as e:
                logger.error(f"Input Error: {e}")

        if self.serial_conn:
            self.serial_conn.close()

if __name__ == "__main__":
    # Allow passing port as argument
    port = SERIAL_PORT
    if len(sys.argv) > 1:
        port = sys.argv[1]
    
    controller = SerialController(port, BAUD_RATE)
    controller.start()
