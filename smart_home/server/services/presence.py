import subprocess
import time
import threading
import sys
import os
from datetime import datetime

# Add parent directory to path to find server_config
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import server_config as config

def log(msg):
    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"[{timestamp}] PRESENCE: {msg}")

class PresenceService:
    def __init__(self, on_change_callback):
        self.is_home = False
        self.running = True
        self.on_change_callback = on_change_callback

    def start(self):
        threading.Thread(target=self._check_loop, daemon=True).start()

    def _check_loop(self):
        while self.running:
            new_state = False
            try:
                # Method 1: PyBluez (Name Lookup)
                # Requires 'pybluez' installed. Works if device is discoverable.
                try:
                    import bluetooth
                    name = bluetooth.lookup_name(config.PHONE_MAC, timeout=5)
                    if name:
                        new_state = True
                except ImportError:
                    pass
                except Exception as e:
                    # print(f"PyBluez Error: {e}") 
                    pass
                
                # Method 2: l2ping (System Command)
                # Works on some hidden devices. Requires sudo often.
                if not new_state:
                    try:
                        cmd = ["l2ping", "-c", "1", "-t", "2", config.PHONE_MAC]
                        # suppress output
                        subprocess.check_output(cmd, stderr=subprocess.DEVNULL)
                        new_state = True
                    except Exception:
                        pass
                
                # Method 3: User's Proven Method (hcitool name)
                if not new_state:
                     try:
                        cmd = ["hcitool", "name", config.PHONE_MAC]
                        result = subprocess.check_output(cmd, stderr=subprocess.DEVNULL).decode()
                        if result.strip():
                             new_state = True
                     except:
                        pass

                # Method 4: Legacy hcitool rssi (Only if connected)
                if not new_state:
                     try:
                        cmd = ["hcitool", "rssi", config.PHONE_MAC]
                        result = subprocess.check_output(cmd, stderr=subprocess.DEVNULL).decode()
                        if result and "RSSI" in result:
                             new_state = True
                     except:
                        pass

                if new_state != self.is_home:
                    self.is_home = new_state
                    print(f"Presence Change: {'HOME' if self.is_home else 'AWAY'}")
                    if self.on_change_callback:
                        self.on_change_callback(self.is_home)

            except Exception as e:
                print(f"Presence Logic Error: {e}")
            
            time.sleep(10)

    def stop(self):
        self.running = False
