import logging
import struct
import time

logger = logging.getLogger("PiController")

class HydrationHandler:
    def __init__(self, controller):
        self.controller = controller
        # We can key off controller.config if attached, or just import config in controller and pass values
        # For now, let's assume controller has access to config or we pass macs
        
    def handle_packet(self, cmd, val, mac):
        # 0x21: REPORT_WEIGHT
        if cmd == 0x21:
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
        
        # 0x51: ALERT_REPLACED
        elif cmd == 0x51:
             logger.info(f"ALERT [{mac}]: Bottle Replaced. Stabilizing...")

        # 0x52: ALERT_REMINDER (NEW)
        elif cmd == 0x52:
             logger.warning(f"ALERT [{mac}]: Hydration Reminder! Drink Detected: NO. User is HOME.")
             
             # Trigger External Actions (IR + LED)
             # IR: 0xF7D02F (Alert)
             if 'ir' in self.controller.handlers:
                 self.controller.handlers['ir'].send_nec("F7D02F")
             
             # LED: Turn Red (RGB 1)
             if 'led' in self.controller.handlers:
                 # 02 12 ... 
                 # Let's just use the send_cmd helper directly if we could, but better to reuse handler logic
                 # Or manually construct packet. LED Handler has send_cmd(hex_payload)
                 # Red = 1.0 -> 00 00 80 3F
                 # Type 2 (LED) Cmd 0x12 (RGB) Data 1.0
                 self.controller.handlers['led'].send_cmd("02120000803F", "ALERT (Red)")

        # 0x53: ALERT_STOPPED (NEW)
        elif cmd == 0x53:
             logger.info(f"ALERT [{mac}]: Hydration Alert STOPPED.")

             # Revert External Actions
             # IR: 0xF7F00F (Normal)
             if 'ir' in self.controller.handlers:
                 self.controller.handlers['ir'].send_nec("F7F00F")

             # LED: Turn Off (or Green? User said "revert them")
             # Let's turn it OFF for now as that's safer than guessing a "normal" color
             if 'led' in self.controller.handlers:
                 # Type 2 (LED) Cmd 0x10 (Main) Data 0.0 (Off)
                 self.controller.handlers['led'].send_cmd("021000000000", "ALERT STOPPED (Off)")

        # 0x60: DRINK_DETECTED
        elif cmd == 0x60:
            logger.info(f"HYDRATION [{mac}]: Drink Detected: {val:.2f} ml")

        # 0x61: DAILY_TOTAL
        elif cmd == 0x61:
            logger.info(f"HYDRATION [{mac}]: Daily Total: {val:.2f} ml")

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

        else:
            logger.warning("Unknown hydration command.")
