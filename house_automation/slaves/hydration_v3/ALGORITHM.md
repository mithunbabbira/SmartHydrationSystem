# Hydration v3 – Algorithm

## Boot

1. Serial, ESP-NOW, Hardware (scale, RGB, LED, buzzer), TimeSync.
2. Load baseline weight and daily total from NVM.
3. Rainbow LED until Pi sends time (`CMD_REPORT_TIME`).

## After time sync

One early `return` for rainbow; after that the loop **never returns early**.
The visual section at the bottom **always runs**.

## Loop sections (in order)

### 1. Sample weight
- Every `WEIGHT_PRINT_INTERVAL_MS`: read weight, send to Pi (`CMD_REPORT_WEIGHT`), print to Serial (daytime only).

### 2. Bottle transitions
- **Weight < THRESHOLD_WEIGHT** → bottle lifted.
  - Mark `bottleMissing = true`, record time.
  - Stop any active drinking alert immediately (`CMD_ALERT_STOPPED`).
- **Weight >= THRESHOLD_WEIGHT while bottleMissing** → bottle returned.
  - Mark `bottleMissing = false`, start stabilization timer.
  - Send `CMD_ALERT_REPLACED` to Pi.

### 3. Stabilization (after bottle return)
- Wait `STABILIZATION_MS` (e.g. 2 s). During this time:
  - Skip interval drinking logic.
  - **Visual section still runs** (shows day/sleep colour).
- When done: **immediately** evaluate weight vs baseline:
  - Drink detected (diff ≥ `DRINK_MIN_DELTA`) → update baseline + daily total in NVM, send `CMD_DRINK_DETECTED` + `CMD_DAILY_TOTAL`, reset interval timer, stop alert.
  - Refill detected (diff ≤ -`REFILL_MIN_DELTA`) → update baseline in NVM, reset interval timer, stop alert.
  - **Anything else** → baseline preserved (small sips accumulate), interval timer reset to prevent rapid re-evaluation, request presence from Pi; if HOME → start drinking alert immediately ("no mercy" – matching old LogicManager.h).

### 4. Interval drinking logic (daytime, bottle present, not stabilizing, no active alert, not waiting for presence)
- Guards: `!drinkAlertActive && !waitingPresence` prevent re-evaluation during an active reminder or while waiting for Pi's presence reply (matching old code's `STATE_WAIT_FOR_PRESENCE` / `STATE_REMINDER_*` which naturally blocked interval checks).
- Every `DRINK_CHECK_INTERVAL_MS`: same evaluation as step 3.
- First time (no baseline in NVM): set baseline, request presence for first alert.

### 5. Visuals (always runs)
Priority order:

| State | RGB | White LED | Buzzer |
|-------|-----|-----------|--------|
| Missing alert active | Red flash | Flash with RGB | Joins after `MISSING_BUZZER_DELAY_MS` |
| Missing, before alert delay | Day/sleep colour | Off | Off |
| Stabilizing | Day/sleep colour | Off | Off |
| Drinking alert (daytime) | Cyan flash | Flash with RGB | Joins after `DRINK_ALERT_BUZZER_DELAY_MS` for `DRINK_ALERT_BUZZER_WINDOW_MS` |
| Sleep | Blue | Off | Off |
| Day (normal) | Day colour | Off | Off |

## Presence gate

Before any drinking alert starts, we send `CMD_REQUEST_PRESENCE` to Pi.
- Pi replies `CMD_REPORT_PRESENCE` with HOME/AWAY.
- **HOME** → `startDrinkAlert()`: set `drinkAlertActive`, init blink timers, send `CMD_ALERT_REMINDER`.
- **AWAY** → skip alert; next interval will retry.

## Drinking alert behaviour

1. Cyan + white LED **blinking** starts immediately.
2. After `DRINK_ALERT_BUZZER_DELAY_MS` (e.g. 5 s) → **buzzer joins** the blink.
3. After `DRINK_ALERT_BUZZER_WINDOW_MS` (e.g. 10 s) → **buzzer stops**, light continues.
4. Alert **never auto-cancels**. Must:
   - Actually drink (weight decrease ≥ `DRINK_MIN_DELTA`), or
   - Lift bottle (missing logic takes over).
5. Picking up the bottle → alert stops instantly, `CMD_ALERT_STOPPED` sent.

## Anti-cheat (matching LinkedIn post)

- If you pick up and put back without drinking:
  - Stabilization happens, weight evaluated, no change detected → presence check → alert starts again immediately.
  - Interval timer IS reset after evaluation (to prevent tight loops), but the presence request is sent instantly so the alert fires without waiting for the next interval.
- While a drinking alert is active, interval evaluation is blocked (`!drinkAlertActive && !waitingPresence` guards). The user MUST lift the bottle to dismiss. When placed back, stabilization → immediate evaluation → alert again if no drink.
- Baseline is **only updated** on confirmed drink or refill, never on "no change".

## NVM persistence

- `baseline_weight`: last confirmed weight after drink/refill.
- `daily_total`: ml consumed today.
- `last_day`: day index for daily reset detection.
- `tare_offset`: scale zero offset.

## ESP-NOW commands (same as v1/v2)

| Slave → Pi | Pi → Slave |
|---|---|
| `CMD_REQUEST_TIME` (0x30) | `CMD_REPORT_TIME` (0x31) |
| `CMD_REPORT_WEIGHT` (0x21) | `CMD_GET_WEIGHT` (0x20) |
| `CMD_REQUEST_PRESENCE` (0x40) | `CMD_REPORT_PRESENCE` (0x41) |
| `CMD_ALERT_MISSING` (0x50) | `CMD_TARE` (0x22) |
| `CMD_ALERT_REPLACED` (0x51) | `CMD_SET_LED` (0x10) |
| `CMD_ALERT_REMINDER` (0x52) | `CMD_SET_BUZZER` (0x11) |
| `CMD_ALERT_STOPPED` (0x53) | |
| `CMD_DRINK_DETECTED` (0x60) | |
| `CMD_DAILY_TOTAL` (0x61) | |

## Config (Config.h)

All timing, thresholds, colours, and pins are defined in `Config.h`.
