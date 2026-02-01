import logging
import struct
import time

from .drink_celebration import (
    trigger as trigger_drink_celebration,
    revert_led_and_ir_to_default,
    LED_RED_PULSE_ALERT_HEX,
)
from . import bottle_alert

logger = logging.getLogger("PiController")

class HydrationHandler:
    def __init__(self, controller):
        self.controller = controller
        self.current_data = {
            'weight': 0.0,
            'status': 'Waiting for data...',
            'last_update': 0,
            'last_drink_ml': 0.0,
            'last_drink_time': 0,
            'daily_total_ml': 0.0,
        }

    def _trigger_alert_display_and_led(self):
        """Alert: display loops rainbow(1s)/text(4s), LED red pulse speed 1, IR flash."""
        if 'ir' in self.controller.handlers:
            self.controller.handlers['ir'].send_nec("F7D02F")
        if 'led' in self.controller.handlers:
            self.controller.handlers['led'].send_cmd(LED_RED_PULSE_ALERT_HEX, "Alert (Red pulse)")
        # Start looping display animation: rainbow 1s -> "no bottle" 4s -> repeat
        bottle_alert.start(self.controller)

    def _revert_alert_display_and_led(self):
        """Revert: stop display alert loop, LED Rainbow speed 5, IR Smooth."""
        bottle_alert.stop(self.controller)
        revert_led_and_ir_to_default(self.controller)

    def handle_packet(self, cmd, val, mac):
        # 0x21: REPORT_WEIGHT
        if cmd == 0x21:
            self.current_data['weight'] = val
            self.current_data['last_update'] = time.time()
            self.current_data['status'] = 'Active'
            logger.info(f"HYDRATION WEIGHT: {val:.2f} g")
        
        # 0x30: REQUEST_TIME from Slave
        elif cmd == 0x30:
            logger.info(f"[{mac}] Requested Time.")
            unix_time = int(time.time())
            time_hex = struct.pack('<I', unix_time).hex()
            self.controller.send_command(mac, "0131" + time_hex)

        # 0x40: REQUEST_PRESENCE from Slave
        elif cmd == 0x40:
            logger.info(f"[{mac}] Requested Presence Check.")
            is_home = self.controller.is_phone_home() # Call back to controller
            payload = "0000803F" if is_home else "00000000" # 1.0 or 0.0
            self.controller.send_command(mac, "0141" + payload)

        # 0x50: ALERT_MISSING
        elif cmd == 0x50:
            logger.warning(f"ALERT [{mac}]: Bottle Missing! (Timer Expired)")
            self._trigger_alert_display_and_led()

        # 0x51: ALERT_REPLACED
        elif cmd == 0x51:
             logger.info(f"ALERT [{mac}]: Bottle Replaced. Stabilizing...")
             self._revert_alert_display_and_led()

        # 0x52: ALERT_REMINDER
        elif cmd == 0x52:
             logger.warning(f"ALERT [{mac}]: Hydration Reminder! Drink Detected: NO. User is HOME.")
             self._trigger_alert_display_and_led()

        # 0x53: ALERT_STOPPED
        elif cmd == 0x53:
             logger.info(f"ALERT [{mac}]: Hydration Alert STOPPED.")
             self._revert_alert_display_and_led()

        # 0x60: DRINK_DETECTED
        elif cmd == 0x60:
            ml = round(val, 1)
            self.current_data['last_drink_ml'] = ml
            self.current_data['last_drink_time'] = time.time()
            self.current_data['last_update'] = time.time()
            logger.info(f"HYDRATION [{mac}]: Drink Detected: {ml} ml")
            if getattr(self.controller, 'append_log_line', None):
                self.controller.append_log_line(f"  >> Drink detected: {ml} ml")
            trigger_drink_celebration(self.controller, ml)

        # 0x61: DAILY_TOTAL
        elif cmd == 0x61:
            ml = round(val, 1)
            self.current_data['daily_total_ml'] = ml
            self.current_data['last_update'] = time.time()
            logger.info(f"HYDRATION [{mac}]: Daily Total: {ml} ml")
            if getattr(self.controller, 'append_log_line', None):
                self.controller.append_log_line(f"  >> Today total: {ml} ml")

    def handle_user_input(self, parts):
        # parts: ['hydration', 'cmd', 'arg']
        # Already checked that parts[0] == 'hydration'
        if len(parts) < 2:
            logger.warning("Usage: hydration <cmd> [args]")
            return

        subcmd = parts[1].lower()
        
        # We need the MAC. 
        # For simplicity, we can import config here or better, get it from controller if available.
        # But controller imports config. Let's try to access it via controller.config if possible,
        # or just fail if not.
        # To strictly follow modularity, we should pass config to __init__. 
        # But simpler:
        import config # This assumes running from same dir context or python path setup.
        
        try:
            mac = config.SLAVE_MACS['hydration']
        except AttributeError:
             # Fallback if config is different structure
             try:
                 mac = config.SLAVE_MACS.get('hydration')
             except:
                 logger.error("Config Error")
                 return
        except KeyError:
            logger.error("Hydration MAC not found in config")
            return

        if subcmd == 'led':
            val = 1 if (len(parts)>2 and parts[2]=='on') else 0
            # 0x10 = SET_LED. Payload: Type(1) Cmd(0x10) Val(float)
            hex_payload = "0110" + ("0000803F" if val else "00000000")
            self.controller.send_command(mac, hex_payload)

        elif subcmd == 'buzzer':
            val = 1 if (len(parts)>2 and parts[2]=='on') else 0
            # 0x11 = SET_BUZZER
            hex_payload = "0111" + ("0000803F" if val else "00000000")
            self.controller.send_command(mac, hex_payload)

        elif subcmd == 'rgb':
            # Valid codes: 0-4
            if len(parts) > 2:
                code = int(parts[2])
                # 0x12 = SET_RGB. Float representation of int code.
                float_hex = struct.pack('<f', code).hex()
                hex_payload = "0112" + float_hex
                self.controller.send_command(mac, hex_payload)
            else:
                logger.warning("Usage: hydration rgb <0-8>")

        elif subcmd == 'weight':
            # 0x20 = GET_WEIGHT. 
            self.controller.send_command(mac, "012000000000")

        elif subcmd == 'tare':
            # 0x22 = CMD_TARE
            self.controller.send_command(mac, "012200000000")
            logger.info("Sent TARE command.")

        elif subcmd == 'test':
             if len(parts) > 2:
                  action = parts[2].lower()
                  if action == 'alert':
                       # Simulate 0x52 ALERT_REMINDER
                       logger.info("TEST: Simulating ALERT START (0x52)...")
                       self.handle_packet(0x52, 0, mac)
                  elif action == 'stop':
                       # Simulate 0x53 ALERT_STOPPED
                       logger.info("TEST: Simulating ALERT STOP (0x53)...")
                       self.handle_packet(0x53, 0, mac)
                  else:
                       logger.warning("Usage: hydration test [alert|stop]")
             else:
                  logger.warning("Usage: hydration test [alert|stop]")

        else:
            logger.warning("Unknown hydration command.")
