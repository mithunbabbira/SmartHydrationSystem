"""
Servo + Adafruit IO spray sequence.

When triggered, runs:
1. Servo axis 3 -> pos 29
2. Adafruit IO command feed -> "SPRAY"
3. Servo axis 0 -> pos 130
4. Wait 3 seconds
5. Servo axis 0 -> pos 0
6. Servo axis 3 -> pos 90

Configure SERVO_BASE_URL and AIO_KEY in config.py or environment.
"""
import logging
import time
from typing import Optional

import requests

import config

logger = logging.getLogger("PiController")

DELAY_SEC = 3


def _servo(axis: int, pos: int, base_url: Optional[str] = None) -> bool:
    """Send servo command. Returns True on success."""
    url = (base_url or config.SERVO_BASE_URL).rstrip("/") + "/api/servo"
    try:
        r = requests.post(
            url,
            headers={"Content-Type": "application/json"},
            json={"axis": axis, "pos": pos},
            timeout=5,
        )
        if r.status_code in (200, 201, 204):
            logger.info("Servo axis=%d pos=%d OK", axis, pos)
            return True
        logger.warning("Servo axis=%d pos=%d HTTP %d: %s", axis, pos, r.status_code, r.text[:200])
        return False
    except Exception as e:
        logger.error("Servo axis=%d pos=%d failed: %s", axis, pos, e)
        return False


def _aio_spray() -> bool:
    """Post SPRAY to Adafruit IO command feed. Returns True on success."""
    if not config.AIO_KEY or config.AIO_KEY == "YOUR_AIO_KEY_HERE":
        logger.error("AIO_KEY not configured")
        return False
    try:
        r = requests.post(
            config.AIO_FEED_URL,
            headers={"X-AIO-Key": config.AIO_KEY, "Content-Type": "application/json"},
            json={"value": "SPRAY"},
            timeout=5,
        )
        if r.status_code in (200, 201, 204):
            logger.info("AIO SPRAY sent OK")
            return True
        logger.warning("AIO SPRAY HTTP %d: %s", r.status_code, r.text[:200])
        return False
    except Exception as e:
        logger.error("AIO SPRAY failed: %s", e)
        return False


def run_sequence(base_url: Optional[str] = None) -> dict:
    """
    Run the full servo + spray sequence.
    Returns dict with status and any error message.
    """
    base = base_url or config.SERVO_BASE_URL
    if not base:
        return {"ok": False, "error": "SERVO_BASE_URL not configured"}

    steps_ok = []
    # 1. Servo axis 3, pos 29
    steps_ok.append(_servo(3, 29, base))
    # 2. Adafruit IO SPRAY
    steps_ok.append(_aio_spray())
    # 3. Servo axis 0, pos 130
    steps_ok.append(_servo(0, 130, base))
    # 4. Wait 3 sec
    time.sleep(DELAY_SEC)
    # 5. Servo axis 0, pos 0
    steps_ok.append(_servo(0, 0, base))
    # 6. Servo axis 3, pos 90
    steps_ok.append(_servo(3, 90, base))

    ok = all(steps_ok)
    if ok:
        logger.info("Servo spray sequence completed")
    else:
        logger.warning("Servo spray sequence had failures: %s", steps_ok)
    return {"ok": ok, "steps": steps_ok}


if __name__ == "__main__":
    import sys

    logging.basicConfig(level=logging.INFO)
    base = sys.argv[1] if len(sys.argv) > 1 else None
    result = run_sequence(base)
    print(result)
    sys.exit(0 if result["ok"] else 1)
