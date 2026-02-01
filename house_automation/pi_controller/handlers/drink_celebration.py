"""
Drink celebration effect: when DRINK_DETECTED is received from hydration slave,
show "drank X.X ml" on display, green LED, green IR for a short duration, then revert.
"""
import logging
import struct
import threading

logger = logging.getLogger("PiController")

DURATION_SEC = 3
IR_GREEN = "F7A05F"
IR_DEFAULT = "F7F00F"  # Smooth / default state
LED_GREEN_HEX = "0212" + struct.pack("<f", 2.0).hex()  # RGB color ID 2 = green
LED_OFF_HEX = "021000000000"

_revert_timer = None
_revert_lock = threading.Lock()


def _revert_to_default(controller):
    """Revert LED and IR to default state after celebration duration."""
    with _revert_lock:
        global _revert_timer
        _revert_timer = None
    if not controller:
        return
    try:
        if "led" in controller.handlers:
            controller.handlers["led"].send_cmd(LED_OFF_HEX, "Drink revert (Off)")
        if "ir" in controller.handlers:
            controller.handlers["ir"].send_nec(IR_DEFAULT)
        logger.info("Drink celebration ended, reverted to default")
    except Exception as e:
        logger.warning("Drink revert failed: %s", e)


def trigger(controller, ml):
    """
    Trigger drink celebration: display text, green LED, green IR for DURATION_SEC.
    After duration, revert LED and IR to default. Cancel any pending revert if
    a new drink arrives before the previous celebration ends.
    """
    global _revert_timer
    if not controller:
        return

    with _revert_lock:
        if _revert_timer is not None:
            _revert_timer.cancel()
            _revert_timer = None

    ml_rounded = round(float(ml), 1)
    text = f"drank {ml_rounded} ml"

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
