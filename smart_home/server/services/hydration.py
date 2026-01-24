import time
from datetime import datetime
import sys
import os

# Add parent directory to path to find server_config
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import server_config as config

def log(msg):
    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"[{timestamp}] HYDRA: {msg}")

class HydrationService:
    def __init__(self, send_command_callback):
        self.last_check_time = time.time()
        self.last_weight = 0
        self.today_consumption = 0
        self.alert_level = 0
        self.snooze_until = 0
        self.daily_reset_day = datetime.now().day
        self.send_command = send_command_callback # Callback to Gateway.send_command
        self.was_missing = False

    def process_weight(self, current_weight, is_home, is_missing=False):
        now = time.time()
        
        # 0. Bottle Missing Logic (Immediate Priority)
        if self.was_missing and not is_missing:
            log("✓ Bottle Replaced.")
            self.alert_level = 0
            self.trigger_alert(0)
        
        self.was_missing = is_missing

        if is_missing:
            # Firmware handles Missing Logic (Timeouts).
            # Server just logs state.
            if self.alert_level != 2:
                 log("⚠ Bottle Missing! (Timer running on Device)")
                 # self.alert_level = 2 # Do not override firmware logic
            return

        # 1. Midnight Reset
        if datetime.now().day != self.daily_reset_day:
            self.today_consumption = 0
            self.daily_reset_day = datetime.now().day
            log("🌅 New Day! Consumption Reset.")

        # 2. Check Interval
        if now - self.last_check_time < config.HYDRATION_CHECK_INTERVAL:
            return

        self.last_check_time = now

        # 3. Validation Checks
        if now < self.snooze_until:
             log("⏸ Snoozed. Skipping check.")
             return
        
        current_hour = datetime.now().hour
        if current_hour >= config.HYDRATION_SLEEP_START or current_hour < config.HYDRATION_SLEEP_END:
            log("😴 Sleep Time. Skipping check.")
            # Ensure silent if sleeping (unless we want to enforce drinking?)
            if self.alert_level > 0:
                self.alert_level = 0
                self.trigger_alert(0)
            return

        if not is_home:
            log("🚶 User Away. Skipping check.")
            if self.alert_level > 0:
                self.alert_level = 0
                self.trigger_alert(0)
            return

        # 4. Weight Analysis
        if self.last_weight == 0:
            self.last_weight = current_weight # Initialize
            self.alert_level = 0 # Reset alerts on first valid weight
            self.trigger_alert(0)
            return

        delta = current_weight - self.last_weight
        print(f"⚖️ Hydration Check: Cur={current_weight}g Prev={self.last_weight}g Delta={delta}g")

        if delta <= -config.HYDRATION_DRINK_THRESHOLD:
            amount = abs(delta)
            self.today_consumption += amount
            self.alert_level = 0
            self.trigger_alert(0)
            print(f"💧 Drink Detected! +{amount}ml (Total: {self.today_consumption}ml)")
            self.last_weight = current_weight
        
        elif delta >= config.HYDRATION_REFILL_THRESHOLD:
            log("🔄 Refill Detected.")
            self.last_weight = current_weight
            self.alert_level = 0
            self.trigger_alert(0)

        else:
            if self.today_consumption < config.HYDRATION_GOAL:
                self.escalate_alert()

    def escalate_alert(self):
        if self.alert_level == 0:
            self.alert_level = 1
            log("🔔 Alert Level 1: Warning")
            self.trigger_alert(1)
        elif self.alert_level == 1:
            self.alert_level = 2
            log("🔔🔔 Alert Level 2: Critical")
            self.trigger_alert(2)

    def trigger_alert(self, level):
        try:
            self.send_command(1, "alert", {"level": level}) 
        except Exception as e:
            print(f"Alert Trigger Failed: {e}")

    def snooze(self, minutes):
        self.snooze_until = time.time() + (minutes * 60)
        print(f"💤 Snoozed for {minutes} min")
        self.alert_level = 0
        self.trigger_alert(0)
        # Also remote snooze
        self.send_command(1, "snooze", None)
