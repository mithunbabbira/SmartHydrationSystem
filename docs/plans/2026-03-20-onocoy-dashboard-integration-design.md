# Onocoy Stations Integration Into Pi Dashboard

## Summary
Add an Onocoy “Stations” panel to the existing Pi Flask dashboard without breaking any current hydration/LED/IR/servo functionality. The dashboard should display Online/Offline (“on/off”) status for all configured Onocoy stations, and preserve existing capabilities: add/remove stations and change the “pool time” (Onocoy polling interval).

## Goals
1. Keep the existing dashboard stable and fast (no external API calls on every UI refresh).
2. Display Onocoy station status (“Online/Offline”) in the same dashboard page as existing features.
3. Implement station management:
   - Add station: `station_id` + `nickname`
   - Remove station: `station_id`
4. Implement “pool time” control:
   - Changes the background polling interval (seconds) used by the Onocoy poller.
5. Validate reliability on a Raspberry Pi:
   - Polling must survive network failures.
   - A slow/broken station fetch must not crash the server.

## Non-Goals (for this step)
- Adding any new Onocoy-controlled device actions beyond displaying status and managing the station registry/settings.
- Rewriting the entire dashboard framework (current Flask + static JS remains).
- Creating a separate Onocoy FastAPI service dependency for the dashboard.

## Current System (as understood)
- The dashboard UI is served by Flask static files in `house_automation/pi_controller/static/`.
- The Pi backend is implemented in `house_automation/pi_controller/web_server.py`.
- Onocoy station polling logic currently exists in `house_automation/onocoy_monitor/onocoy_station_monitor/main.py` (FastAPI).
- Onocoy station registry and settings are persisted as JSON files already present in the repo root:
  - `onocoy_stations.json`
  - `onocoy_settings.json`

## Recommended Approach
Integrate Onocoy polling directly into the existing Flask server via:
1. A background polling thread that:
   - Loads `onocoy_stations.json` + `onocoy_settings.json`
   - Polls `https://api.onocoy.com/api/v1/explorer/server/{station_id}/info`
   - Updates an in-memory cached store (fast reads for the UI)
2. Flask endpoints that return the cached store and allow add/remove/manage-settings.

This avoids dashboard latency from external API calls and reduces moving parts for reliability.

## Architecture

### Components
1. `OnocoyStationStore` (in-memory cache)
   - `stations: dict[station_id] -> { nickname, status, last_updated, last_checked }`
   - A `threading.Lock` guards mutations and reads.
2. `OnocoyPollerThread` (background)
   - Runs an infinite loop with sleep = `polling_interval` (seconds)
   - For each station_id:
     - Fetch Onocoy info with a short timeout
     - Extract:
       - `is_up` -> `status = "Online" | "Offline"`
       - `since` -> `last_updated`
     - Set `last_checked` to local ISO timestamp
     - On errors:
       - log the error
       - update `last_checked` if desired, but do not crash
3. Persistence helpers
   - `load_stations()` / `save_stations()`
   - `load_settings()` / `save_settings()`

### Endpoints (Flask)
1. `GET /api/onocoy/status`
   - Returns cached station dictionary as JSON.
2. `POST /api/onocoy/manage-station`
   - Form/JSON fields:
     - `action`: `"add"` | `"remove"`
     - `station_id`: required
     - `nickname`: required only for add (fallback to station_id)
3. `POST /api/onocoy/manage-settings`
   - Fields:
     - `polling_interval`: integer seconds, min enforced (e.g. >= 5)

### Dashboard UI contract
The front-end expects:
- Each station has:
  - `nickname`
  - `status` (Online/Offline)
  - `last_updated`
  - `last_checked`

## UI Changes (Static Files)
Update the dashboard page without altering current hydration/LED/IR UI:
- `house_automation/pi_controller/static/index.html`
  - Add a new “Onocoy Stations” card/section.
  - Add:
    - Pool time input (`polling_interval`, seconds)
    - Add station form (station_id + nickname)
    - Remove station form (station_id)
    - Station table/list container
- `house_automation/pi_controller/static/app.js`
  - Add polling to fetch `/api/onocoy/status` and render rows.
  - Add form handlers:
    - POST add/remove/manage-settings to Flask endpoints.
  - Render Online/Offline with visual styling.
- `house_automation/pi_controller/static/style.css`
  - Add minimal new styles for the Onocoy card elements.

UI refresh cadence should be lightweight (e.g. every 2–3 seconds) and must not call Onocoy directly—only the cached Flask endpoint.

## Error Handling & Reliability
1. Background poller:
   - Catch network exceptions per station.
   - Use request timeout (e.g. 10 seconds) and continue.
   - Polling interval changes should take effect quickly:
     - simplest: poller reads `settings` each cycle
     - safer: store `polling_interval` in a thread-safe variable updated by the settings endpoint
2. Flask endpoints:
   - If cache is empty/uninitialized, return `{}` plus a helpful field if needed.
3. Thread safety:
   - Always lock around reading and writing the station store.

## Validation Plan
After implementation:
1. Confirm existing endpoints still work:
   - `/api/data`
   - `/api/master/log`
   - hydration/LED/IR/servo endpoints
2. Confirm Onocoy endpoints:
   - `GET /api/onocoy/status` returns JSON with station entries
   - add/remove station persists to `onocoy_stations.json`
   - manage-settings persists to `onocoy_settings.json`
3. Confirm UI:
   - Onocoy card renders station rows with Online/Offline states
   - “Pool time” updates are reflected (poller cadence changes over time)
4. Pi reliability check:
   - Simulate Onocoy API failure (or watch logs) and ensure Flask stays responsive.

## Files to Modify (Implementation Step)
- `house_automation/pi_controller/web_server.py`
- `house_automation/pi_controller/static/index.html`
- `house_automation/pi_controller/static/app.js`
- `house_automation/pi_controller/static/style.css`

