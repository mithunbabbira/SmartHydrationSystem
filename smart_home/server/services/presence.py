import subprocess
import time
import threading
import sys
import os

# Add parent directory to path to find server_config
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import server_config as config

class PresenceService:
    def __init__(self, on_change_callback):
        self.is_home = False
        self.running = True
        self.on_change_callback = on_change_callback

    def start(self):
        threading.Thread(target=self._check_loop, daemon=True).start()

    def _check_loop(self):
        while self.running:
            try:
                # Use hcitool to check RSSI (fast check)
                cmd = ["hcitool", "rssi", config.PHONE_MAC]
                result = subprocess.check_output(cmd, stderr=subprocess.STDOUT).decode()
                new_state = bool(result.strip())
                
                if new_state != self.is_home:
                    self.is_home = new_state
                    print(f"Presence Change: {'HOME' if self.is_home else 'AWAY'}")
                    if self.on_change_callback:
                        self.on_change_callback(self.is_home)
                    
            except Exception as e:
                # hcitool might return error if device not found
                if self.is_home:
                    self.is_home = False
                    print("Presence Change: AWAY (Detection Error/Timeout)")
                    if self.on_change_callback:
                        self.on_change_callback(False)
            
            time.sleep(10)

    def stop(self):
        self.running = False
