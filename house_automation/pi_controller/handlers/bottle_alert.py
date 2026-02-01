"""
Bottle missing alert animation: alternates rainbow (1s) and "no bottle" text (4s).
Loops until stopped. Modular and cancellable.
"""
import logging
import threading

logger = logging.getLogger("PiController")

RAINBOW_SEC = 1
TEXT_SEC = 4
TEXT_MSG = "no bottle"

_alert_thread = None
_alert_stop_event = threading.Event()
_alert_lock = threading.Lock()


def _alert_loop(controller):
    """Internal loop: rainbow -> text -> repeat until stop event."""
    while not _alert_stop_event.is_set():
        ono = controller.handlers.get("ono") if controller else None
        if not ono:
            break

        # Phase 1: Rainbow
        try:
            ono.send_rainbow(RAINBOW_SEC)
        except Exception as e:
            logger.warning("Bottle alert rainbow failed: %s", e)

        # Wait for rainbow duration (check stop event frequently)
        if _alert_stop_event.wait(timeout=RAINBOW_SEC):
            break

        # Phase 2: "no bottle" text
        try:
            ono.send_text(TEXT_MSG, TEXT_SEC)
        except Exception as e:
            logger.warning("Bottle alert text failed: %s", e)

        # Wait for text duration
        if _alert_stop_event.wait(timeout=TEXT_SEC):
            break

    logger.info("Bottle alert loop ended")


def start(controller):
    """Start the bottle missing alert animation loop."""
    global _alert_thread
    with _alert_lock:
        # Stop any existing alert first
        if _alert_thread is not None and _alert_thread.is_alive():
            _alert_stop_event.set()
            _alert_thread.join(timeout=2)

        _alert_stop_event.clear()
        _alert_thread = threading.Thread(target=_alert_loop, args=(controller,), daemon=True)
        _alert_thread.start()
        logger.info("Bottle alert animation started")


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
