# Hydration v3 – Algorithm

## Config (Config.h)

- **Sleep time:** `SLEEP_START_HOUR` and `SLEEP_END_HOUR` (24h). Example: 23 and 10 → sleep from 23:00 to 09:59, awake from 10:00.
- **Pi:** `MASTER_MAC_BYTES` for ESP-NOW.
- **Pins, scale:** `CALIBRATION_FACTOR`, pin defines.
- **Time sync:** `TIME_SYNC_TIMEOUT_MS`, `TIME_SYNC_REQUEST_MS`.
- **Weight print:** `WEIGHT_PRINT_INTERVAL_MS` (how often to print weight when awake).

## Boot flow

1. **Setup**
   - Serial, ESP-NOW (Comms), Hardware (scale, RGB, LED, buzzer), TimeSync.
   - Log: "Rainbow on. Requesting time from Pi...".

2. **Loop (until time synced)**
   - Process any incoming ESP-NOW packet (Pi can send `CMD_REPORT_TIME`).
   - Request time from Pi every `TIME_SYNC_REQUEST_MS` (non-blocking).
   - Run **rainbow** on RGB every loop.
   - When Pi sends time (`CMD_REPORT_TIME`), TimeSync stores offset; next loop we are “synced”.

3. **Loop (after time synced)**
   - Stop rainbow (RGB off, once).
   - Compute current hour from synced time.
   - **Sleep window:** if `hour >= SLEEP_START_HOUR` or `hour < SLEEP_END_HOUR` → do nothing (no weight print).
   - **Awake:** every `WEIGHT_PRINT_INTERVAL_MS` print current weight to Serial.

## Summary

| Phase        | RGB        | Action |
|-------------|------------|--------|
| Boot, no time | Rainbow  | Request time from Pi; handle incoming packets. |
| Time synced   | Off      | If sleep time → nothing. If awake → print weight every N ms. |

## ESP-NOW (same as before)

- **Slave → Pi:** `CMD_REQUEST_TIME` (0x30), `CMD_REPORT_WEIGHT` (0x21), etc.
- **Pi → Slave:** `CMD_REPORT_TIME` (0x31), `CMD_GET_WEIGHT` (0x20), `CMD_TARE` (0x22), `CMD_SET_LED`, `CMD_SET_BUZZER`, …
- Packet format unchanged (ControlPacket: type, command, data).

## Files

- **Config.h** – Sleep time, pins, MAC, time sync and weight-print intervals.
- **Comms.h** – ESP-NOW init, send, receive; global `incomingPacket`, `packetReceived`.
- **TimeSync.h** – Request time, store offset when Pi replies; `getHour()`, `getDay()`.
- **Hardware.h** – Scale (HX711), RGB, rainbow, LED, buzzer.
- **hydration_v3.ino** – setup/loop, process packets, rainbow until sync, then sleep check + weight print.
