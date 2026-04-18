import json
import os
import threading
from datetime import datetime, timezone


def _utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


class OnocoyStationStore:
    """
    Thread-safe in-memory cache of Onocoy stations + JSON persistence.
    The dashboard reads from this cache; the poller updates it.
    """

    def __init__(
        self,
        stations_path: str,
        settings_path: str,
        min_poll_interval_sec: int = 5,
        default_poll_interval_sec: int = 60,
    ):
        # RLock prevents deadlocks when a public method calls another method that
        # also needs to hold the same lock (e.g., add_station -> ensure_station_exists).
        self._lock = threading.RLock()
        self.stations_path = stations_path
        self.settings_path = settings_path
        self.min_poll_interval_sec = int(min_poll_interval_sec)
        self.default_poll_interval_sec = int(default_poll_interval_sec)

        self.stations: dict = {}
        self.settings: dict = {"polling_interval": self.default_poll_interval_sec}

        self._load_from_disk()

    def _load_from_disk(self) -> None:
        with self._lock:
            self.stations = self._safe_read_json(self.stations_path, default={})
            self.settings = self._safe_read_json(
                self.settings_path,
                default={"polling_interval": self.default_poll_interval_sec},
            )

            # Enforce min polling interval
            pi = self._to_int(self.settings.get("polling_interval"), self.default_poll_interval_sec)
            self.settings["polling_interval"] = max(self.min_poll_interval_sec, pi)
            self._safe_write_json(self.settings_path, self.settings)

    def _safe_read_json(self, path: str, default):
        try:
            if not os.path.exists(path):
                # Ensure file exists to keep behavior consistent with your current app.
                self._safe_write_json(path, default)
                return default
            with open(path, "r") as f:
                data = json.load(f)
                return data if data is not None else default
        except Exception:
            # If JSON is corrupted, fall back to defaults rather than crashing the server.
            return default

    def _safe_write_json(self, path: str, data) -> None:
        try:
            tmp = path + ".tmp"
            with open(tmp, "w") as f:
                json.dump(data, f, indent=2)
            os.replace(tmp, path)
        except Exception:
            # Persistence failures must never crash the server loop.
            pass

    def _to_int(self, v, default: int) -> int:
        try:
            return int(v)
        except Exception:
            return int(default)

    def get_snapshot(self) -> dict:
        # Deep copy via JSON roundtrip (small dict; reliability over micro-optimization).
        with self._lock:
            return json.loads(json.dumps(self.stations))

    def save_stations(self) -> None:
        with self._lock:
            self._safe_write_json(self.stations_path, self.stations)

    def get_polling_interval(self) -> int:
        with self._lock:
            return int(self.settings.get("polling_interval", self.default_poll_interval_sec))

    def ensure_station_exists(self, station_id: str, nickname: str | None = None) -> None:
        with self._lock:
            if station_id not in self.stations:
                self.stations[station_id] = {
                    "nickname": nickname if nickname is not None else station_id,
                    "status": "Offline",
                    "last_updated": None,
                    "last_checked": None,
                }

    def add_station(self, station_id: str, nickname: str | None = None) -> None:
        with self._lock:
            self.ensure_station_exists(station_id, nickname=nickname)
            if nickname is not None:
                self.stations[station_id]["nickname"] = nickname

    def remove_station(self, station_id: str) -> None:
        with self._lock:
            if station_id in self.stations:
                del self.stations[station_id]

    def set_polling_interval(self, polling_interval_sec: int) -> int:
        with self._lock:
            pi = self._to_int(polling_interval_sec, self.default_poll_interval_sec)
            pi = max(self.min_poll_interval_sec, pi)
            self.settings["polling_interval"] = pi
            self._safe_write_json(self.settings_path, self.settings)
            return pi

    def update_station_from_onocoy_info(self, station_id: str, info: dict, now_iso: str | None = None) -> None:
        """
        Update a station's cached status using the response JSON from Onocoy.
        Expected shape:
          info["status"]["is_up"] -> bool
          info["status"]["since"] -> str timestamp
        """
        now_iso = now_iso or _utc_now_iso()
        raw_status = {}
        try:
            raw_status = (info or {}).get("status", {}) if isinstance(info, dict) else {}
        except Exception:
            raw_status = {}

        is_up = bool(raw_status.get("is_up", False))
        since = raw_status.get("since")

        with self._lock:
            if station_id not in self.stations:
                # Do not recreate removed station automatically.
                return
            self.stations[station_id]["status"] = "Online" if is_up else "Offline"
            self.stations[station_id]["last_updated"] = since
            self.stations[station_id]["last_checked"] = now_iso

