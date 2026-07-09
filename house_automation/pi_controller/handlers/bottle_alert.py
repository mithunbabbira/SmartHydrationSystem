"""
Hydration alert display animation: alternates rainbow (1s) and custom text (4s).
Loops until stopped. Use text_msg="no bottle" for bottle missing, "plz drink" for reminder.
"""
import logging
import threading

logger = logging.getLogger("PiController")

RAINBOW_SEC = 1
TEXT_SEC = 4
DEFAULT_TEXT_BOTTLE_MISSING = "no bottle"
DEFAULT_TEXT_DRINK_REMINDER = "plz drink"

_alert_thread = None
_alert_stop_event = threading.Event()
_alert_lock = threading.Lock()
_current_text_msg = DEFAULT_TEXT_BOTTLE_MISSING


def _alert_loop(controller):
    """Internal loop: rainbow -> text -> repeat until stop event."""
    global _current_text_msg
    while not _alert_stop_event.is_set():
        ono = controller.handlers.get("ono") if controller else None
        if not ono:
            break

        # Phase 1: Rainbow
        try:
            ono.send_rainbow(RAINBOW_SEC)
        except Exception as e:
            logger.warning("Alert rainbow failed: %s", e)

        # Wait for rainbow duration (check stop event frequently)
        if _alert_stop_event.wait(timeout=RAINBOW_SEC):
            break

        # Phase 2: Custom text (e.g. "no bottle" or "plz drink")
        msg = _current_text_msg
        try:
            ono.send_text(msg, TEXT_SEC)
        except Exception as e:
            logger.warning("Alert text failed: %s", e)

        # Wait for text duration
        if _alert_stop_event.wait(timeout=TEXT_SEC):
            break

    logger.info("Alert display loop ended")


def start(controller, text_msg=None):
    """Start the alert display loop. text_msg: e.g. 'no bottle' or 'plz drink'."""
    global _alert_thread, _current_text_msg
    _current_text_msg = (text_msg or DEFAULT_TEXT_BOTTLE_MISSING).strip() or DEFAULT_TEXT_BOTTLE_MISSING
    with _alert_lock:
        # Stop any existing alert first
        if _alert_thread is not None and _alert_thread.is_alive():
            _alert_stop_event.set()
            _alert_thread.join(timeout=2)

        _alert_stop_event.clear()
        _alert_thread = threading.Thread(target=_alert_loop, args=(controller,), daemon=True)
        _alert_thread.start()
        logger.info("Alert display started: %s", _current_text_msg)


def stop(controller):
    """Stop the bottle missing alert animation."""
    global _alert_thread
    with _alert_lock:
        if _alert_thread is not None:
            _alert_stop_event.set()
            _alert_thread.join(timeout=2)
            _alert_thread = None
            logger.info("Bottle alert animation stopped")


def is_active():
    """Check if alert animation is currently running."""
    with _alert_lock:
        return _alert_thread is not None and _alert_thread.is_alive()
