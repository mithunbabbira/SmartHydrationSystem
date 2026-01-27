import logging
import struct

logger = logging.getLogger("PiController")

class IRHandler:
    def __init__(self, controller):
        self.controller = controller
        
    def handle_packet(self, cmd, val, mac):
        # IR Remote probably won't send much back
        logger.info(f"IR [{mac}] -> Cmd:0x{cmd:02X} Val:{val:.2f}")

    def send_nec(self, hex_code):
        try:
            code_val = int(hex_code, 16)
            # Protocol: Type=3 (IR), Cmd=0x31 (NEC)
            # Payload: 32-bit integer packed (Little Endian)
            code_hex = struct.pack('<I', code_val).hex()
            payload = "0331" + code_hex
            
            # Send via Controller
            # We must get the proper MAC. IR Handler usually just controls one remote, 
            # but for generality let's get it from config if possible or default to configured
            import config
            mac = config.SLAVE_MACS.get('ir_remote', '00:00:00:00:00:00')
            
            if mac != '00:00:00:00:00:00':
                 self.controller.send_command(mac, payload)
                 logger.info(f"Sent IR NEC: 0x{code_val:08X}")
            else:
                 logger.error("Cannot send NEC: IR MAC not configured")

        except ValueError:
            logger.error(f"Invalid Hex Code: {hex_code}")

    def handle_user_input(self, parts):
        # parts: ['ir', 'cmd', 'arg']
        if len(parts) < 2:
            logger.warning("Usage: ir send <HEX_CODE>")
            return

        subcmd = parts[1].lower()

        if subcmd == 'send':
             if len(parts) > 2:
                  self.send_nec(parts[2])
             else:
                  logger.warning("Usage: ir send <HEX_CODE>")
        else:
             logger.warning("Unknown IR command. Try: send <HEX_CODE>")
