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

    def handle_user_input(self, parts):
        # parts: ['led', 'cmd', 'arg']
        if len(parts) < 2:
            logger.warning("Usage: led <cmd> [args] (e.g. led on, led rgb 1)")
            return

        import config 
        try:
            mac = config.SLAVE_MACS['led_ble']
        except KeyError:
            logger.error("LED MAC not found in config")
            return
            
        if mac == '00:00:00:00:00:00':
             logger.warning("LED MAC is not configured! Check config.py.")
             return

        subcmd = parts[1].lower()

        if subcmd == 'on':
            # 0x10 = SET_LED. Payload: 1.0
            self.controller.send_command(mac, "01100000803F")
            logger.info("Sent LED ON")

        elif subcmd == 'off':
            # 0x10 = SET_LED. Payload: 0.0
            self.controller.send_command(mac, "011000000000")
            logger.info("Sent LED OFF")

        elif subcmd == 'rgb':
             # led rgb <id>
             if len(parts) > 2:
                  try:
                      code = int(parts[2])
                      # 0x12 = SET_RGB. Float representation
                      float_hex = struct.pack('<f', code).hex()
                      hex_payload = "0112" + float_hex
                      self.controller.send_command(mac, hex_payload)
                      logger.info(f"Sent LED RGB {code}")
                  except ValueError:
                      logger.error("Invalid RGB code. Use integer.")
             else:
                  logger.warning("Usage: led rgb <id>")

        elif subcmd == 'raw':
             # led raw <HEX_PAYLOAD>
             if len(parts) > 2:
                  hex_payload = parts[2]
                  if all(c in '0123456789ABCDEFabcdef' for c in hex_payload) and len(hex_payload) % 2 == 0:
                      self.controller.send_command(mac, hex_payload)
                      logger.info(f"Sent LED RAW: {hex_payload}")
                  else:
                      logger.error("Invalid HEX payload.")
             else:
                  logger.warning("Usage: led raw <HEX_PAYLOAD>")
        
        else:
             logger.warning("Unknown LED command. Try: on, off, rgb <id>, raw <hex>")
