"""
Drink celebration effect: when DRINK_DETECTED is received from hydration slave,
show "X.X ml" on display, green LED, green IR for a short duration, then revert.

Shared LED/display defaults:
- Default LED: Rainbow mode 37, speed 5
- Alert LED: Red pulse mode 39, speed 1
"""
import logging
import struct
import threading

logger = logging.getLogger("PiController")

DURATION_SEC = 3
IR_GREEN = "F7A05F"
IR_DEFAULT = "F7F00F"  # Smooth / default state
LED_GREEN_HEX = "0212" + struct.pack("<f", 2.0).hex()  # RGB color ID 2 = green
LED_RAINBOW_DEFAULT_HEX = "0213" + struct.pack("<I", (37 << 8) | 5).hex()  # Rainbow speed 5
LED_RED_PULSE_ALERT_HEX = "0213" + struct.pack("<I", (38 << 8) | 1).hex()  # Red pulse (mode 38) speed 1

_revert_timer = None
_revert_lock = threading.Lock()


def revert_led_and_ir_to_default(controller):
    """Revert LED to Rainbow speed 5 and IR to default (Smooth)."""
    if not controller:
        return
    try:
        if "led" in controller.handlers:
            controller.handlers["led"].send_cmd(LED_RAINBOW_DEFAULT_HEX, "Default (Rainbow)")
        if "ir" in controller.handlers:
            controller.handlers["ir"].send_nec(IR_DEFAULT)
        logger.info("Reverted LED and IR to default")
    except Exception as e:
        logger.warning("Revert to default failed: %s", e)


def _revert_to_default(controller):
    """Revert LED and IR to default after celebration duration."""
    with _revert_lock:
        global _revert_timer
        _revert_timer = None
    revert_led_and_ir_to_default(controller)


def trigger(controller, ml):
    """
    Trigger drink celebration: display "X.X ml", green LED, green IR for DURATION_SEC.
    After duration, revert LED to Rainbow speed 5 and IR to default.
    """
    global _revert_timer
    if not controller:
        return

    with _revert_lock:
        if _revert_timer is not None:
            _revert_timer.cancel()
            _revert_timer = None

    ml_rounded = round(float(ml), 1)
    text = f"{ml_rounded} ml"

    try:
        if "ono" in controller.handlers:
            controller.handlers["ono"].send_text(text, DURATION_SEC)
        if "led" in controller.handlers:
            controller.handlers["led"].send_cmd(LED_GREEN_HEX, "Drink (Green)")
        if "ir" in controller.handlers:
            controller.handlers["ir"].send_nec(IR_GREEN)
        logger.info("Drink celebration: %s for %ds", text, DURATION_SEC)
    except Exception as e:
        logger.warning("Drink celebration trigger failed: %s", e)
        return

    with _revert_lock:
        _revert_timer = threading.Timer(DURATION_SEC, _revert_to_default, args=[controller])
        _revert_timer.daemon = True
        _revert_timer.start()
