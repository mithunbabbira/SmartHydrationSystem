# Hydration v3 - Stability Algorithm

## Boot sequence

1. Init Serial, ESP-NOW, hardware, and TimeSync.
2. Load `baseline_weight`, `daily_total`, and day index from NVM.
3. Show rainbow while requesting time from Pi.
4. If time sync is not received within timeout:
   - exit rainbow mode,
   - continue hydration logic in daytime fallback mode,
   - skip day-reset logic until sync arrives.

## Time handling

- Pi sends **local epoch** in `CMD_REPORT_TIME (0x31)`.
- Slave uses that epoch directly (no hardcoded timezone offset in firmware).
- Sleep window remains `23:00-10:00` when synced.

## Startup hardening (runs once after sync/timeout)

1. Read a stable startup weight (multi-sample average).
2. Detect initial bottle state:
   - below `THRESHOLD_WEIGHT` -> bottle missing.
   - otherwise -> bottle present.
3. Baseline policy:
   - If bottle missing at boot: `baselineValid=false` (relearn on return).
   - If bottle present and no baseline in NVM: initialize baseline from current stable weight.
   - If bottle present and baseline exists:
     - if drift >= `BOOT_REBASE_DELTA`, silently rebase baseline to current weight.
4. If NVM baseline existed, force immediate first daytime evaluation (no bottle lift needed).

## Runtime loop (after startup init)

### 1) Periodic weight report

- Every `WEIGHT_PRINT_INTERVAL_MS`: send `CMD_REPORT_WEIGHT (0x21)` to Pi.

### 2) Bottle state transitions (jitter hardened)

- Uses hysteresis + confirmation samples:
  - Missing candidate: `< THRESHOLD_WEIGHT`.
  - Present candidate: `> THRESHOLD_WEIGHT + BOTTLE_HYSTERESIS_G`.
  - Transition only after `BOTTLE_CONFIRM_SAMPLES` confirmations at `BOTTLE_SAMPLE_INTERVAL_MS`.

Transitions:
- Present -> Missing:
  - clear transient states,
  - stop active drink alert,
  - start missing timer.
- Missing -> Present:
  - send `CMD_ALERT_REPLACED (0x51)`,
  - enter stabilization timer.

### 3) Stabilization evaluation

- After return, wait `STABILIZATION_MS`.
- Evaluate using confirmed weight (two-read confirm, median fallback on drift):
  - Drink: `baseline - weight >= DRINK_MIN_DELTA`
    - send `CMD_DRINK_DETECTED (0x60)` and `CMD_DAILY_TOTAL (0x61)`.
  - Refill: `baseline - weight <= -REFILL_MIN_DELTA`
    - update baseline only.
  - No change: preserve baseline, request presence.

### 4) Interval evaluation (daytime)

Runs only when:
- not sleep,
- bottle present,
- not stabilizing,
- no active drink alert,
- not waiting for presence,
- retry backoff window (if any) has elapsed.

Then:
- if baseline invalid: set baseline only.
- else if `DRINK_CHECK_INTERVAL_MS` elapsed: evaluate.

### 5) Presence gate + timeout hardening

- Before reminder alert, slave sends `CMD_REQUEST_PRESENCE (0x40)`.
- Pi replies `CMD_REPORT_PRESENCE (0x41)`:
  - HOME -> start drink alert (`CMD_ALERT_REMINDER`, 0x52).
  - AWAY -> skip reminder.
- If no reply within `PRESENCE_REPLY_TIMEOUT_MS`:
  - clear waiting state,
  - set retry-not-before to `PRESENCE_RETRY_AFTER_TIMEOUT_MS`,
  - continue normal loop (no stuck state).

## Visual priority (always runs)

1. Missing alert active
2. Stabilizing
3. Drinking alert
4. Sleep color
5. Day color

Missing and reminder paths keep the same behavior:
- lights first,
- buzzer joins after configured delay.

## NVM keys

- `baseline_weight`
- `daily_total`
- `last_day`
- `tare_offset`

## Command map (unchanged IDs)

| Slave -> Pi | Pi -> Slave |
|---|---|
| `CMD_REQUEST_TIME` (0x30) | `CMD_REPORT_TIME` (0x31) |
| `CMD_REPORT_WEIGHT` (0x21) | `CMD_GET_WEIGHT` (0x20) |
| `CMD_REQUEST_PRESENCE` (0x40) | `CMD_REPORT_PRESENCE` (0x41) |
| `CMD_ALERT_MISSING` (0x50) | `CMD_TARE` (0x22) |
| `CMD_ALERT_REPLACED` (0x51) | `CMD_SET_LED` (0x10) |
| `CMD_ALERT_REMINDER` (0x52) | `CMD_SET_BUZZER` (0x11) |
| `CMD_ALERT_STOPPED` (0x53) | `CMD_REQUEST_DAILY_TOTAL` (0x23) |
| `CMD_DRINK_DETECTED` (0x60) | |
| `CMD_DAILY_TOTAL` (0x61) | |

