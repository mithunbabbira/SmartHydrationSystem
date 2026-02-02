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
from collections import deque

# Import Handlers
from handlers.hydration import HydrationHandler
from handlers.led import LEDHandler
from handlers.ir import IRHandler
from handlers.ono import OledHandler

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
        # Ring buffer of raw lines from master (for dashboard log)
        self._serial_log = deque(maxlen=500)
        self._log_lock = threading.Lock()

        # Initialize Handlers
        self.handlers = {
            'hydration': HydrationHandler(self),
            'led': LEDHandler(self),
            'ir': IRHandler(self),
            'ono': OledHandler(self),
        }

    def _close_serial(self):
        try:
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.close()
        except Exception as e:
            logger.debug(f"Close serial: {e}")
        self.serial_conn = None
        if getattr(self, "watchdog", None):
            self.watchdog.serial_conn = None

    def _reconnect_serial(self):
        self._close_serial()
        try:
            self.serial_conn = serial.Serial(self.port, self.baud_rate, timeout=1)
            self.serial_conn.dtr = False
            time.sleep(0.1)
            self.serial_conn.dtr = True
            if getattr(self, "watchdog", None):
                self.watchdog.serial_conn = self.serial_conn
            logger.info(f"Reconnected to {self.port} at {self.baud_rate} baud.")
            return True
        except serial.SerialException as e:
            logger.warning(f"Reconnect failed: {e}")
            return False

    def connect(self):
        try:
            self.serial_conn = serial.Serial(self.port, self.baud_rate, timeout=1)
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
        while self.running:
            try:
                if not self.serial_conn or not self.serial_conn.is_open:
                    self._reconnect_serial()
                    if not self.serial_conn:
                        time.sleep(5)
                    continue
                if self.serial_conn.in_waiting > 0:
                    line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        with self._log_lock:
                            self._serial_log.append({"t": time.time(), "line": line})
                        self.watchdog.pet()
                        self.process_incoming_data(line)
                else:
                    # Avoid busy-loop: sleep when no data (major cause of high CPU / Pi instability)
                    time.sleep(0.02)
            except (serial.SerialException, OSError, IOError) as e:
                logger.error(f"Serial error (will reconnect): {e}")
                self._close_serial()
                time.sleep(2)
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
        # Only process RX lines from Master (ignore HEARTBEAT, OK:, ERR:); match RX anywhere in line in case of leading garbage
        match = re.search(r'RX:([0-9A-Fa-f:]+):([0-9A-Fa-f\s]+)', line)
        if match:
            mac = match.group(1)
            hex_data = match.group(2).replace(' ', '').replace('\r', '').strip()
            try:
                data_bytes = bytes.fromhex(hex_data)
                
                # Check for Hydration or Protocol Commands (6 bytes)
                if len(data_bytes) == 6:
                    ctype, cmd, val = struct.unpack('<BBf', data_bytes)
                    if ctype == 1:
                        self.handlers['hydration'].handle_packet(cmd, val, mac)
                    elif ctype == 2:
                        self.handlers['led'].handle_packet(cmd, val, mac)
                    elif ctype == 3:
                        self.handlers['ono'].handle_packet(cmd, val, mac)
                    else:
                        logger.info(f"UNKNOWN TYPE [{mac}] -> Type:{ctype} Cmd:0x{cmd:02X} Val:{val:.2f}")
                elif len(data_bytes) >= 2 and data_bytes[0] == 3:
                    self.handlers['ono'].handle_packet(data_bytes[1], 0, mac)
                else:
                    logger.info(f"DATA [{mac}] -> RAW HEX: {hex_data}")
            except Exception as e:
                logger.error(f"Failed to decode data from {mac}: {e}")

    def send_command(self, mac_address, hex_data):
        command = f"TX:{mac_address}:{hex_data}\n"
        try:
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.write(command.encode('utf-8'))
                with self._log_lock:
                    self._serial_log.append({"t": time.time(), "line": f">> TX {mac_address} {hex_data}"})
                logger.info(f"SENT to {mac_address}: {hex_data}")
            else:
                logger.error("Serial connection lost. Cannot send.")
        except (serial.SerialException, OSError) as e:
            logger.error(f"Serial send failed: {e}")
            self._close_serial()
        except Exception as e:
            logger.error(f"Error sending data: {e}")

    def get_serial_log(self, limit=200):
        """Return last `limit` lines from master serial (for dashboard)."""
        with self._log_lock:
            return list(self._serial_log)[-limit:]

    def append_log_line(self, msg):
        """Append a human-readable line to the serial log (e.g. drink detected, today total)."""
        with self._log_lock:
            self._serial_log.append({"t": time.time(), "line": msg})

    def health(self):
        """Return dict with serial status and last activity for monitoring."""
        with self._log_lock:
            last = list(self._serial_log)[-1] if self._serial_log else None
        return {
            "serial_connected": bool(self.serial_conn and self.serial_conn.is_open),
            "serial_port": self.port,
            "last_line_time": last["t"] if last else None,
            "log_entries": len(self._serial_log),
        }

    def start(self, headless=False):
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
        
        if not headless:
            self.ui_loop()
        else:
            logger.info("Controller started in HEADLESS mode.")

    def ui_loop(self):
        print("\n--- House Automation Controller (Modular) ---")
        print("Commands: hydration <cmd>, led <cmd>, ono <cmd>")
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

        self._close_serial()

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
        if not self.serial_conn or not self.serial_conn.is_open:
            logger.warning("Cannot reset master: serial not connected.")
            return
        try:
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
