import logging
import struct

logger = logging.getLogger("PiController")

class LEDHandler:
    def __init__(self, controller):
        self.controller = controller
        
    def handle_packet(self, cmd, val, mac):
        # Currently the LED strip doesn't send much back except maybe ACKs or Status if we implemented it
        # But if we receive something from the LED MAC, we can log it.
        logger.info(f"LED [{mac}] -> Cmd:0x{cmd:02X} Val:{val:.2f}")

    def send_cmd(self, hex_payload, description="CMD"):
        import config
        mac = config.SLAVE_MACS.get('led_ble', '00:00:00:00:00:00')
        if mac != '00:00:00:00:00:00':
             self.controller.send_command(mac, hex_payload)
             logger.info(f"Sent LED {description}")
        else:
             logger.error("LED MAC not configured")

    def handle_user_input(self, parts):
        # parts: ['led', 'cmd', 'arg']
        if len(parts) < 2:
            logger.warning("Usage: led <cmd> [args] (e.g. led on, led rgb 1)")
            return

        subcmd = parts[1].lower()
        
        # Helper to get mac not strictly needed here if we use send_cmd, 
        # but for specific Logic inside cases we might use send_cmd.

        if subcmd == 'on':
        if subcmd == 'on':
            # 0x10 = SET_LED. Payload: 1.0
            self.send_cmd("02100000803F", "ON")

        elif subcmd == 'off':
        elif subcmd == 'off':
            # 0x10 = SET_LED. Payload: 0.0
            self.send_cmd("021000000000", "OFF")

        elif subcmd == 'rgb':
             # led rgb <id>
             if len(parts) > 2:
                  try:
                      code = int(parts[2])
                      # 0x12 = SET_RGB. Float representation
                      float_hex = struct.pack('<f', code).hex()
                      hex_payload = "0212" + float_hex
                      self.send_cmd(hex_payload, f"RGB {code}")
                  except ValueError:
                      logger.error("Invalid RGB code. Use integer.")
             else:
                  logger.warning("Usage: led rgb <id>")

        elif subcmd == 'mode':
             # led mode <id> <speed>
             if len(parts) > 3:
                  try:
                      mode = int(parts[2])
                      speed = int(parts[3])
                      # 0x13 = CMD_SET_MODE
                      # Data: 0x0000[MODE][SPEED] -> Little Endian Bytes: [SPEED][MODE][00][00]
                      # Packed as uint32 (I)
                      # Value = (mode << 8) | speed
                      val = (mode << 8) | speed
                      # Struct pack <I (Little Endian uint32)
                      hex_payload = "0213" + struct.pack('<I', val).hex()
                      self.send_cmd(hex_payload, f"Mode {mode} Speed {speed}")
                  except ValueError:
                      logger.error("Usage: led mode <id> <speed> (integers)")
             else:
                  logger.warning("Usage: led mode <id> <speed>")

        elif subcmd == 'rainbow':
             # Fast Rainbow Shortcut (Mode 37, Speed 100)
             # Val = (37 << 8) | 100 = 0x2564
             val = (37 << 8) | 100
             hex_payload = "0213" + struct.pack('<I', val).hex()
             self.send_cmd(hex_payload, "Rainbow Mode")

        elif subcmd == 'raw':
             # led raw <HEX_PAYLOAD>
             if len(parts) > 2:
                  hex_payload = parts[2]
                  if all(c in '0123456789ABCDEFabcdef' for c in hex_payload) and len(hex_payload) % 2 == 0:
                      self.send_cmd(hex_payload, f"RAW: {hex_payload}")
                  else:
                      logger.error("Invalid HEX payload.")
             else:
                  logger.warning("Usage: led raw <HEX_PAYLOAD>")
        
        else:
             logger.warning("Unknown LED command. Try: on, off, rgb <id>, raw <hex>")
