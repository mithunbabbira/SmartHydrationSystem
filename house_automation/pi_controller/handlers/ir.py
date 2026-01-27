import logging
import struct

logger = logging.getLogger("PiController")

class IRHandler:
    def __init__(self, controller):
        self.controller = controller
        
    def handle_packet(self, cmd, val, mac):
        # IR Remote probably won't send much back
        logger.info(f"IR [{mac}] -> Cmd:0x{cmd:02X} Val:{val:.2f}")

    def handle_user_input(self, parts):
        # parts: ['ir', 'cmd', 'arg']
        if len(parts) < 2:
            logger.warning("Usage: ir send <HEX_CODE>")
            return

        import config 
        try:
            mac = config.SLAVE_MACS['ir_remote']
        except KeyError:
             logger.error("IR MAC not found in config")
             return

        if mac == '00:00:00:00:00:00':
             logger.warning("IR Remote MAC is not configured! Check config.py.")
             return

        subcmd = parts[1].lower()

        if subcmd == 'send':
             if len(parts) > 2:
                  hex_code = parts[2]
                  try:
                      code_val = int(hex_code, 16)
                      
                      # Protocol: 0x31 = CMD_SEND_NEC
                      # Payload: 32-bit integer packed
                      # Hex formatting: 01 31 AABBCCDD
                      
                      # Struct pack <I (Little Endian uint32)
                      code_hex = struct.pack('<I', code_val).hex()
                      
                      # Full Packet: Type(01) Cmd(31) Data(Code)
                      # Wait, type 03 for IR? 
                      # The ESP8266 code expects IncomingPacket type struct
                      # IncomingPacket struct: Type(u8), Command(u8), Data(u32)
                      # My controller usually sends RAW HEX bytes which are interpreted as Type,Cmd,Data
                      
                      # Let's use Type=3 for IR 
                      payload = "0331" + code_hex
                      
                      self.controller.send_command(mac, payload)
                      logger.info(f"Sent IR NEC: 0x{code_val:08X}")
                  except ValueError:
                      logger.error("Invalid Hex Code")
             else:
                  logger.warning("Usage: ir send <HEX_CODE>")
        else:
             logger.warning("Unknown IR command. Try: send <HEX_CODE>")
