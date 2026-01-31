"""Handler for ONO display (OLED + RGB) – text, rainbow, custom color via ESP-NOW."""
import logging
import struct
import config

logger = logging.getLogger("PiController")

# Protocol: Type 3 = ONO display
# Cmd 0x50 = Rainbow (6 bytes: 03 50 + duration float)
# Cmd 0x51 = Color (8 bytes: 03 51 R G B + duration float)
# Cmd 0x60 = Text (9+ bytes: 03 60 + duration float + len + UTF-8 text)
# Cmd 0x70 = Price update (10 bytes: 03 70 + price_usd float + change_24h float)

class OledHandler:
    def __init__(self, controller):
        self.controller = controller

    def handle_packet(self, cmd, val, mac):
        logger.info(f"ONO [{mac}] -> Cmd:0x{cmd:02X} Val:{val:.2f}")

    def _display_macs(self):
        """Return list of display MACs to send to (ono_display + cam_display)."""
        macs = []
        for key in ('ono_display', 'cam_display'):
            m = config.SLAVE_MACS.get(key, '00:00:00:00:00:00')
            if m != '00:00:00:00:00:00':
                macs.append(m)
        return macs

    def send_cmd(self, hex_payload, description="CMD"):
        macs = self._display_macs()
        if not macs:
            logger.error("No display MAC configured (ono_display / cam_display)")
            return
        for mac in macs:
            self.controller.send_command(mac, hex_payload)
        logger.info(f"Sent ONO {description} to {len(macs)} display(s)")

    def send_rainbow(self, duration_sec=10):
        """Rainbow effect for `duration_sec` seconds."""
        payload = "03" + "50" + struct.pack('<f', float(duration_sec)).hex()
        self.send_cmd(payload, f"Rainbow {duration_sec}s")

    def send_color(self, r, g, b, duration_sec=10):
        """Custom RGB color for `duration_sec` seconds."""
        r = max(0, min(255, int(r)))
        g = max(0, min(255, int(g)))
        b = max(0, min(255, int(b)))
        payload = "03" + "51" + f"{r:02x}{g:02x}{b:02x}" + struct.pack('<f', float(duration_sec)).hex()
        self.send_cmd(payload, f"Color R{r} G{g} B{b} {duration_sec}s")

    def send_text(self, text, duration_sec=5):
        """Display text (scrolls if long) for `duration_sec` seconds."""
        text = (text or "").strip()
        if not text:
            logger.warning("ONO text empty")
            return
        raw = text.encode('utf-8')
        if len(raw) > 80:
            raw = raw[:80]
        payload = "03" + "60" + struct.pack('<f', float(duration_sec)).hex() + f"{len(raw):02x}" + raw.hex()
        self.send_cmd(payload, f"Text '{text[:20]}...' {duration_sec}s")

    def send_price(self, price_usd, change_24h):
        """Send ONO price and 24h change to display (Pi fetches from CoinGecko)."""
        try:
            p = float(price_usd)
            c = float(change_24h)
        except (TypeError, ValueError):
            logger.warning("ONO send_price: invalid numbers")
            return
        payload = "03" + "70" + struct.pack('<f', p).hex() + struct.pack('<f', c).hex()
        self.send_cmd(payload, f"Price ${p:.4f} 24h {c:+.2f}%")

    def handle_user_input(self, parts):
        if len(parts) < 2:
            logger.warning("Usage: ono <rainbow|color|text> [args]")
            return
        subcmd = parts[1].lower()
        if subcmd == 'rainbow':
            dur = int(parts[2]) if len(parts) > 2 else 10
            self.send_rainbow(dur)
        elif subcmd == 'color':
            if len(parts) >= 6:
                r, g, b = int(parts[2]), int(parts[3]), int(parts[4])
                dur = int(parts[5]) if len(parts) > 5 else 10
                self.send_color(r, g, b, dur)
            else:
                logger.warning("Usage: ono color <r> <g> <b> [duration_sec]")
        elif subcmd == 'text':
            text = " ".join(parts[2:]) if len(parts) > 2 else ""
            dur = 5
            if text and text.split()[-1].isdigit():
                dur = int(text.split()[-1])
                text = " ".join(text.split()[:-1])
            self.send_text(text or "Hello", dur)
        else:
            logger.warning("Unknown ono cmd. Try: rainbow, color, text")
