# Onocoy Dashboard Integration Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Integrate Onocoy station polling + station management into the existing Pi Flask dashboard while keeping current hydration/LED/IR/servo functionality stable.

**Architecture:** Run an Onocoy poller in a background thread inside `pi_controller/web_server.py`, store station state in an in-memory cache with a lock, expose fast Flask endpoints that return cached state, and extend the existing static dashboard UI to render the cached station “Online/Offline” statuses plus add/remove/pool-time controls.

**Tech Stack:** Python 3, Flask, `requests`, `threading`, JSON file persistence (`onocoy_stations.json`, `onocoy_settings.json`), vanilla JS for dashboard updates.

---

### Task 0: Ensure dedicated worktree (if none)

**Files:**
- None

**Step 1: Write the failing test**
- N/A

**Step 2: Run test to verify it fails**
- N/A

**Step 3: Write minimal implementation**
- Create a dedicated git worktree to follow the intended workflow (safe, not destructive):
  - Run: `git worktree add ../onocoy-dashboard-integration-worktree`

**Step 4: Run test to verify it passes**
- Run: `git worktree list` and confirm the new worktree exists.

**Step 5: Commit**
- No commit (worktree creation is local).

---

### Task 1: Create Onocoy station store + normalizer module

**Files:**
- Create: `house_automation/pi_controller/onocoy_station_store.py`
- Test: `tests/house_automation/pi_controller/test_onocoy_station_store.py`

**Step 1: Write the failing test**
```python
# tests/house_automation/pi_controller/test_onocoy_station_store.py
import json
import os
import tempfile
import unittest

from house_automation.pi_controller.onocoy_station_store import (
    OnocoyStationStore,
)


class TestOnocoyStationStore(unittest.TestCase):
    def test_load_defaults_when_files_missing(self):
        with tempfile.TemporaryDirectory() as td:
            stations_path = os.path.join(td, "onocoy_stations.json")
            settings_path = os.path.join(td, "onocoy_settings.json")
            store = OnocoyStationStore(
                stations_path=stations_path,
                settings_path=settings_path,
                min_poll_interval_sec=5,
            )
            # Should not crash; should initialize to empty stations and default interval
            self.assertIsInstance(store.stations, dict)
            self.assertIsInstance(store.settings, dict)
            self.assertGreaterEqual(store.settings.get("polling_interval", 0), 5)

    def test_update_mapping_online_offline(self):
        with tempfile.TemporaryDirectory() as td:
            stations_path = os.path.join(td, "onocoy_stations.json")
            settings_path = os.path.join(td, "onocoy_settings.json")
            store = OnocoyStationStore(
                stations_path=stations_path,
                settings_path=settings_path,
                min_poll_interval_sec=5,
            )
            station_id = "TEST1"
            now_iso = "2026-03-20T00:00:00+00:00"
            since = "2026-03-19T00:00:00+00:00"
            store.ensure_station_exists(station_id, nickname="Nick")
            store.update_station_from_onocoy_info(
                station_id=station_id,
                info={"status": {"is_up": True, "since": since}},
                now_iso=now_iso,
            )
            self.assertEqual(store.stations[station_id]["status"], "Online")
            self.assertEqual(store.stations[station_id]["last_updated"], since)
            self.assertEqual(store.stations[station_id]["last_checked"], now_iso)

    def test_add_and_remove_station_persist(self):
        with tempfile.TemporaryDirectory() as td:
            stations_path = os.path.join(td, "onocoy_stations.json")
            settings_path = os.path.join(td, "onocoy_settings.json")
            store = OnocoyStationStore(
                stations_path=stations_path,
                settings_path=settings_path,
                min_poll_interval_sec=5,
            )
            store.add_station("S1", nickname="A")
            store.save_stations()
            with open(stations_path, "r") as f:
                raw = json.load(f)
            self.assertIn("S1", raw)
            store.remove_station("S1")
            store.save_stations()
            with open(stations_path, "r") as f:
                raw2 = json.load(f)
            self.assertNotIn("S1", raw2)


if __name__ == "__main__":
    unittest.main()
```

**Step 2: Run test to verify it fails**
- Run: `python -m unittest -v tests.house_automation.pi_controller.test_onocoy_station_store`
- Expected: FAIL because `onocoy_station_store.py` does not exist yet.

**Step 3: Write minimal implementation**
Create `house_automation/pi_controller/onocoy_station_store.py` with:
```python
import json
import os
import threading
from datetime import datetime, timezone


def _utc_now_iso():
    return datetime.now(timezone.utc).isoformat()


class OnocoyStationStore:
    def __init__(self, stations_path, settings_path, min_poll_interval_sec=5, default_poll_interval_sec=60):
        self._lock = threading.Lock()
        self.stations_path = stations_path
        self.settings_path = settings_path
        self.min_poll_interval_sec = int(min_poll_interval_sec)
        self.default_poll_interval_sec = int(default_poll_interval_sec)

        self.stations = {}
        self.settings = {"polling_interval": self.default_poll_interval_sec}
        self._load_from_disk()

    def _load_from_disk(self):
        with self._lock:
            if os.path.exists(self.stations_path):
                with open(self.stations_path, "r") as f:
                    self.stations = json.load(f) or {}
            else:
                self.stations = {}
                self._safe_write_json(self.stations_path, self.stations)

            if os.path.exists(self.settings_path):
                with open(self.settings_path, "r") as f:
                    self.settings = json.load(f) or {}
            else:
                self.settings = {"polling_interval": self.default_poll_interval_sec}
                self._safe_write_json(self.settings_path, self.settings)

            pi = int(self.settings.get("polling_interval", self.default_poll_interval_sec))
            self.settings["polling_interval"] = max(self.min_poll_interval_sec, pi)
            self._safe_write_json(self.settings_path, self.settings)

    def _safe_write_json(self, path, data):
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(data, f, indent=2)
        os.replace(tmp, path)

    def save_stations(self):
        with self._lock:
            self._safe_write_json(self.stations_path, self.stations)

    def save_settings(self):
        with self._lock:
            self._safe_write_json(self.settings_path, self.settings)

    def get_snapshot(self):
        with self._lock:
            return json.loads(json.dumps(self.stations))

    def ensure_station_exists(self, station_id, nickname=None):
        with self._lock:
            if station_id not in self.stations:
                self.stations[station_id] = {
                    "nickname": (nickname if nickname is not None else station_id),
                    "status": "Offline",
                    "last_updated": None,
                    "last_checked": None,
                }

    def add_station(self, station_id, nickname=None):
        with self._lock:
            self.ensure_station_exists(station_id, nickname=nickname)
            self.stations[station_id]["nickname"] = (nickname if nickname is not None else station_id)

    def remove_station(self, station_id):
        with self._lock:
            if station_id in self.stations:
                del self.stations[station_id]

    def set_polling_interval(self, polling_interval_sec):
        with self._lock:
            pi = int(polling_interval_sec)
            self.settings["polling_interval"] = max(self.min_poll_interval_sec, pi)

    def update_station_from_onocoy_info(self, station_id, info, now_iso=None):
        now_iso = now_iso or _utc_now_iso()
        raw_status = (info or {}).get("status", {}) if isinstance(info, dict) else {}
        is_up = bool(raw_status.get("is_up", False))
        since = raw_status.get("since")

        with self._lock:
            if station_id not in self.stations:
                # In case station was removed between cycles, don't recreate unexpectedly
                return
            self.stations[station_id]["status"] = "Online" if is_up else "Offline"
            self.stations[station_id]["last_updated"] = since
            self.stations[station_id]["last_checked"] = now_iso
```

**Step 4: Run test to verify it passes**
- Run: `python -m unittest -v tests.house_automation.pi_controller.test_onocoy_station_store`
- Expected: PASS

**Step 5: Commit**
- Commit:
  - `git add house_automation/pi_controller/onocoy_station_store.py tests/house_automation/pi_controller/test_onocoy_station_store.py`
  - `git commit -m "feat(onocoy): add station store + tests"`

---

### Task 2: Add Onocoy poller thread and Flask endpoints

**Files:**
- Modify: `house_automation/pi_controller/web_server.py`

**Step 1: Write the failing test**
- N/A (integration endpoints; rely on unit tests from Task 1 and manual validation).

**Step 2: Run test to verify it fails**
- N/A

**Step 3: Write minimal implementation**
In `web_server.py`:
1. Create store using repo-root JSON paths:
   - `stations_path = os.path.join(repo_root, "onocoy_stations.json")`
   - `settings_path = os.path.join(repo_root, "onocoy_settings.json")`
2. Start poller inside `start_controller()` (after controller is created) so Flask server is up.
3. Poller behavior:
   - Use a daemon thread `OnocoyPollerThread`.
   - Each loop:
     - Read station IDs snapshot under lock (or from store.stations keys copy).
     - For each station_id:
       - GET Onocoy info with timeout (e.g. 10s)
       - call `store.update_station_from_onocoy_info(...)`
   - Sleep for `store.settings["polling_interval"]`.
4. Add Flask routes:
   - `@app.get("/api/onocoy/status")` -> `store.get_snapshot()`
   - `@app.post("/api/onocoy/manage-station")`
     - Accept JSON with `action`, `station_id`, `nickname`
     - action add/remove
     - persist to disk
     - return `{ "status": "ok" }` or `400/503`
   - `@app.post("/api/onocoy/manage-settings")`
     - Accept JSON `{ polling_interval }`
     - enforce min
     - persist
5. Make sure failures do not crash the Flask process:
   - Wrap poller loop in try/except.
   - Log exceptions with the WebServer logger.

Code skeleton to add (exact place varies):
```python
from onocoy_station_store import OnocoyStationStore

onocoy_store = None

def start_onocoy_poller():
    global onocoy_store
    if onocoy_store is not None:
        return
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    stations_path = os.path.join(repo_root, "onocoy_stations.json")
    settings_path = os.path.join(repo_root, "onocoy_settings.json")
    onocoy_store = OnocoyStationStore(
        stations_path=stations_path,
        settings_path=settings_path,
        min_poll_interval_sec=5,
        default_poll_interval_sec=60,
    )

    def _poll_loop():
        import requests
        url_tmpl = "https://api.onocoy.com/api/v1/explorer/server/{station_id}/info"
        session = requests.Session()
        while True:
            try:
                snapshot = onocoy_store.get_snapshot()
                station_ids = list(snapshot.keys())
                with onocoy_store._lock:
                    interval = int(onocoy_store.settings.get("polling_interval", 60))

                for station_id in station_ids:
                    url = url_tmpl.format(station_id=station_id)
                    try:
                        r = session.get(url, timeout=10)
                        if r.status_code == 200:
                            info = r.json()
                            onocoy_store.update_station_from_onocoy_info(
                                station_id=station_id,
                                info=info,
                                now_iso=None,
                            )
                        else:
                            # Mark offline on non-200, but do not crash
                            onocoy_store.update_station_from_onocoy_info(
                                station_id=station_id,
                                info={"status": {"is_up": False, "since": None}},
                                now_iso=None,
                            )
                    except Exception as e:
                        # Keep going; set checked timestamp to indicate liveness
                        onocoy_store.update_station_from_onocoy_info(
                            station_id=station_id,
                            info={"status": {"is_up": False, "since": None}},
                            now_iso=None,
                        )

                # Persist updated statuses
                onocoy_store.save_stations()
                time.sleep(interval)
            except Exception as e:
                logger.error("Onocoy poller crashed; restarting loop: %s", e)
                time.sleep(10)

    threading.Thread(target=_poll_loop, daemon=True).start()
```

**Step 4: Run tests and verify**
1. Run: `python -m py_compile house_automation/pi_controller/web_server.py`
2. Start the server and verify:
   - `GET /api/onocoy/status` returns JSON.
   - `POST /api/onocoy/manage-station` adds/removes and is reflected in `GET`.
   - `POST /api/onocoy/manage-settings` updates polling interval.

**Step 5: Commit**
- Commit:
  - `git add house_automation/pi_controller/web_server.py house_automation/pi_controller/onocoy_station_store.py`
  - `git commit -m "feat(onocoy): add poller + status endpoints"`

---

### Task 3: Add Onocoy panel markup to the dashboard

**Files:**
- Modify: `house_automation/pi_controller/static/index.html`

**Step 1: Write the failing test**
- N/A (UI).

**Step 2: Run test to verify it fails**
- N/A

**Step 3: Write minimal implementation**
Add a new card/section in `index.html` (e.g., near other cards) with IDs:
- `onocoy-card`
- `onocoy-pool-time-input` (number input)
- `onocoy-pool-time-save-btn`
- `onocoy-add-station-id-input`
- `onocoy-add-station-nickname-input`
- `onocoy-add-btn`
- `onocoy-remove-station-id-input`
- `onocoy-remove-btn`
- `onocoy-stations-container` (div/table body)
Populate it with accessible labels.

Example inserted HTML block (exact placement can vary):
```html
<div class="card onocoy-card">
  <div class="card-header">
    <div class="icon-box"><i class="fa-solid fa-gear"></i></div>
    <h2>Onocoy Stations</h2>
  </div>
  <div class="card-body">
    <div class="section-label">Pool time (poll interval seconds)</div>
    <div class="raw-row" style="gap: 8px;">
      <input type="number" id="onocoy-pool-time-input" min="5" value="60" style="width:6em;">
      <button type="button" id="onocoy-pool-time-save-btn" class="btn-glass secondary">Save</button>
    </div>

    <hr style="margin: 14px 0; border-color: rgba(255,255,255,0.1);">

    <div class="section-label">Add Station</div>
    <div class="raw-row">
      <input type="text" id="onocoy-add-station-id-input" placeholder="Station ID" class="raw-input">
    </div>
    <div class="raw-row" style="margin-top: 8px;">
      <input type="text" id="onocoy-add-station-nickname-input" placeholder="Nickname" class="raw-input">
    </div>
    <div class="control-group" style="margin-top: 10px;">
      <button type="button" id="onocoy-add-btn" class="btn-glass success">Add / Update</button>
    </div>

    <div class="section-label" style="margin-top: 16px;">Remove Station</div>
    <div class="raw-row">
      <input type="text" id="onocoy-remove-station-id-input" placeholder="Station ID" class="raw-input">
    </div>
    <div class="control-group" style="margin-top: 10px;">
      <button type="button" id="onocoy-remove-btn" class="btn-glass danger">Remove</button>
    </div>

    <hr style="margin: 14px 0; border-color: rgba(255,255,255,0.1);">

    <div class="section-label">Status</div>
    <div id="onocoy-stations-container" class="onocoy-stations-empty">Loading...</div>
  </div>
</div>
```

**Step 4: Run tests and verify**
- Manual:
  - Refresh dashboard.
  - Ensure existing UI doesn’t break layout.

**Step 5: Commit**
- `git add house_automation/pi_controller/static/index.html`
- `git commit -m "feat(ui): add Onocoy stations card"`

---

### Task 4: Implement Onocoy dashboard rendering + controls

**Files:**
- Modify: `house_automation/pi_controller/static/app.js`

**Step 1: Write the failing test**
- N/A.

**Step 2: Run test to verify it fails**
- N/A.

**Step 3: Write minimal implementation**
In `app.js`:
1. Add `fetchOnocoyStatus()`:
   - `fetch('/api/onocoy/status')`
   - On success: render each station row.
2. Render row:
   - station_id key
   - nickname
   - status with class `online`/`offline`
   - last_updated and last_checked (formatted with existing helpers or simple functions)
3. Add station management handlers:
   - `POST /api/onocoy/manage-station` with JSON:
     - `{ action: "add", station_id, nickname }`
     - `{ action: "remove", station_id }`
   - After success: refresh status.
4. Pool time save handler:
   - `POST /api/onocoy/manage-settings` with JSON `{ polling_interval }`
   - Update input on success.
5. Poll UI cadence:
   - Keep it separate from pool time, e.g. update UI every 2 seconds using cached endpoint.

Example code to append (or integrate near other functions):
```javascript
function formatLocalDateTime(iso) {
  if (!iso) return '--';
  try {
    const d = new Date(iso);
    if (isNaN(d.getTime())) return String(iso);
    return d.toLocaleString();
  } catch (e) {
    return String(iso);
  }
}

function renderOnocoyStations(stations) {
  const el = document.getElementById('onocoy-stations-container');
  if (!el) return;
  const keys = Object.keys(stations || {});
  if (keys.length === 0) {
    el.innerHTML = 'No stations configured. Add one above.';
    return;
  }
  el.innerHTML = '';
  keys.forEach((stationId) => {
    const info = stations[stationId] || {};
    const status = info.status || 'Offline';
    const cls = status === 'Online' ? 'onocoy-online' : 'onocoy-offline';
    const row = document.createElement('div');
    row.className = 'onocoy-station-row ' + cls;
    row.innerHTML = `
      <div class="onocoy-station-id"><b>${stationId}</b></div>
      <div class="onocoy-station-nick">${info.nickname || '--'}</div>
      <div class="onocoy-station-status">${status}</div>
      <div class="onocoy-station-times">
        <div>Since: ${formatLocalDateTime(info.last_updated)}</div>
        <div>Checked: ${formatLocalDateTime(info.last_checked)}</div>
      </div>
    `;
    el.appendChild(row);
  });
}

function fetchOnocoyStatus() {
  fetch('/api/onocoy/status')
    .then(r => r.json())
    .then(data => renderOnocoyStations(data))
    .catch(err => {
      console.error('Onocoy status fetch failed:', err);
      const el = document.getElementById('onocoy-stations-container');
      if (el) el.innerHTML = 'Onocoy status unavailable (check logs).';
    });
}

function setupOnocoyControls() {
  const poolInput = document.getElementById('onocoy-pool-time-input');
  const saveBtn = document.getElementById('onocoy-pool-time-save-btn');
  const addBtn = document.getElementById('onocoy-add-btn');
  const removeBtn = document.getElementById('onocoy-remove-btn');
  const addId = document.getElementById('onocoy-add-station-id-input');
  const addNick = document.getElementById('onocoy-add-station-nickname-input');
  const removeId = document.getElementById('onocoy-remove-station-id-input');

  if (saveBtn && poolInput) {
    saveBtn.addEventListener('click', () => {
      const polling_interval = parseInt(poolInput.value, 10);
      fetch('/api/onocoy/manage-settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ polling_interval })
      }).then(r => r.json()).then(() => fetchOnocoyStatus());
    });
  }

  if (addBtn && addId) {
    addBtn.addEventListener('click', () => {
      const station_id = (addId.value || '').trim();
      const nickname = (addNick && addNick.value) ? addNick.value.trim() : station_id;
      if (!station_id) return alert('Enter station_id');
      fetch('/api/onocoy/manage-station', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'add', station_id, nickname })
      }).then(r => r.json()).then(() => fetchOnocoyStatus());
    });
  }

  if (removeBtn && removeId) {
    removeBtn.addEventListener('click', () => {
      const station_id = (removeId.value || '').trim();
      if (!station_id) return alert('Enter station_id to remove');
      fetch('/api/onocoy/manage-station', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'remove', station_id })
      }).then(r => r.json()).then(() => fetchOnocoyStatus());
    });
  }
}

document.addEventListener('DOMContentLoaded', () => {
  setupOnocoyControls();
  fetchOnocoyStatus();
  setInterval(fetchOnocoyStatus, 2000);
});
```

**Step 4: Run tests and verify**
- Manual:
  - UI renders Onocoy rows.
  - Add station shows up.
  - Remove station disappears.
  - Pool time update changes how quickly statuses change (watch `last_checked` updates).

**Step 5: Commit**
- `git add house_automation/pi_controller/static/app.js`
- `git commit -m "feat(ui): render Onocoy stations + controls"`

---

### Task 5: Add minimal CSS for Onocoy panel

**Files:**
- Modify: `house_automation/pi_controller/static/style.css`

**Step 1: Write the failing test**
- N/A

**Step 2: Run test to verify it fails**
- N/A

**Step 3: Write minimal implementation**
Add CSS classes:
```css
.onocoy-station-row {
  display: grid;
  grid-template-columns: 1.2fr 0.8fr;
  gap: 6px 12px;
  padding: 10px 12px;
  margin-top: 8px;
  background: rgba(255,255,255,0.04);
  border: 1px solid rgba(255,255,255,0.08);
  border-radius: 10px;
}
.onocoy-station-status { grid-column: 1 / -1; font-weight: 700; }
.onocoy-online .onocoy-station-status { color: var(--success); }
.onocoy-offline .onocoy-station-status { color: var(--danger); }
.onocoy-stations-empty { color: var(--text-muted); }
```

**Step 4: Run tests and verify**
- Manual: ensure Onocoy panel looks good without affecting other cards.

**Step 5: Commit**
- `git add house_automation/pi_controller/static/style.css`
- `git commit -m "style(ui): add Onocoy panel styles"`

---

### Task 6: Reliability validation pass (no new code)

**Files:**
- None

**Step 1: Write failing test**
- N/A

**Step 2: Run test to verify it fails**
- N/A

**Step 3: Write minimal implementation**
- Manual checklist:
  - Load dashboard; verify hydration section still updates every 2 seconds.
  - Load Onocoy status; verify UI renders and doesn’t freeze.
  - Change pool time; verify statuses keep updating (or slow down) without restarting Pi.
  - Add/remove stations; verify persistence to `onocoy_stations.json` and no duplicate crashes.

**Step 4: Run tests and verify it passes**
- Observe Pi logs:
  - Onocoy poller errors are logged but server remains responsive.

**Step 5: Commit**
- No commit unless small bug fixes needed.

---

### Task 7: Final integration commit (if needed)
If any missing edge cases appear (e.g., station_id sanitization, JSON parse errors), implement fixes and commit.

