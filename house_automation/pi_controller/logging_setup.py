"""
Logging setup for smart-home: rotating file + console.
- Old logs are automatically deleted by rotation (no manual cleanup needed).
- Max disk use: 1 MB × 4 files = ~4 MB. When current file hits 1 MB it rotates;
  the oldest backup (.log.3) is removed so the system never overloads.
"""
import logging
import os
import sys

# Log directory: next to this file, then 'logs/'
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")
LOG_FILE = os.path.join(LOG_DIR, "smart-home.log")
LOG_MAX_BYTES = 1 * 1024 * 1024   # 1 MB per file; when exceeded, rotate (old .log.3 deleted)
LOG_BACKUP_COUNT = 3               # keep .log + .log.1, .log.2, .log.3 → total 4 files max


def setup_logging():
    """Add rotating file handler to root logger. Call once at app startup."""
    os.makedirs(LOG_DIR, exist_ok=True)
    from logging.handlers import RotatingFileHandler

    formatter = logging.Formatter(
        "%(asctime)s - %(name)s - %(levelname)s - %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    file_handler = RotatingFileHandler(
        LOG_FILE,
        maxBytes=LOG_MAX_BYTES,
        backupCount=LOG_BACKUP_COUNT,
        encoding="utf-8",
    )
    file_handler.setFormatter(formatter)
    file_handler.setLevel(logging.DEBUG)

    root = logging.getLogger()
    root.setLevel(logging.DEBUG)
    # Avoid duplicate handlers if setup is called twice
    if not any(isinstance(h, RotatingFileHandler) for h in root.handlers):
        root.addHandler(file_handler)
    # Ensure at least one console handler so we still see output in journal
    if not any(isinstance(h, logging.StreamHandler) for h in root.handlers):
        console = logging.StreamHandler(sys.stdout)
        console.setFormatter(formatter)
        console.setLevel(logging.INFO)
        root.addHandler(console)
    # So we see in journal that rotation is active and old logs are auto-deleted
    root.info("Logging to %s (rotating, max 4 MB total; old logs auto-deleted)", LOG_FILE)

    return LOG_DIR
