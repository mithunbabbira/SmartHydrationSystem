# Hydration System – New Version Plan (v2)

Planning document for a cleaner, more maintainable hydration system. **Review and adjust before implementation.**

---

## 1. Current State (v1) Summary

### Slave (ESP32)

| File | Role |
|------|------|
| `hydration.ino` | Entry, time sync loop, setup/loop, packet dispatch |
| `SlaveComms.h` | ESP-NOW to Master, packet struct, commands enum |
| `SlaveConfig.h` | Timeouts, thresholds, colors |
| `LogicManager.h` | State machine (MONITORING → REMINDER → STABILIZING, etc.) |
| `Hardware.h` | Scale, LED, buzzer, RGB, NVM |

### Pi Controller

| Area | Role |
|------|------|
| `controller.py` | Serial RX/TX, dispatch by type/cmd, time push at startup |
| `handlers/hydration.py` | Handle 0x21, 0x30, 0x40, 0x50–0x53, 0x60, 0x61; trigger alerts |
| `handlers/drink_celebration.py` | Drink detected: display, LED, IR, revert |
| `handlers/bottle_alert.py` | Alert display loop: rainbow 1s + text 4s |

### Protocol

- **Type 1**, 6-byte: `[type=1][cmd][data=4 bytes]`. Commands 0x10–0x61 (see SLAVES_AND_DASHBOARD.md).

---

## 2. Pain Points / Lessons Learned

1. **Time sync** – Slave blocked until 0x31; if Slave→Master→Pi failed, it hung. Fix: timeout (60s) + Pi startup time push.
2. **Boot without bottle** – Stale NVM baseline caused false “drank” when bottle placed. Fix: clear baseline when boot with no bottle; in stabilise, “no baseline” branch sets baseline only.
3. **False drink at interval** – Single noisy weight read triggered 0x60. Fix: confirm with second read after 400 ms.
4. **Code structure** – Logic in one big `LogicManager.h`; time sync and packet handling mixed in main .ino. Hard to navigate and test.
5. **Magic numbers** – Some timeouts/periods in code; config could be single source of truth.
6. **Alert path** – If Pi never receives 0x50/0x52 (Master not forwarding), LED/IR/display don’t trigger; need clear debugging path.

---

## 3. Goals for v2

- **Clarity** – Clear separation: time, comms, hardware, logic, config.
- **Reliability** – No indefinite blocking; time sync with timeout + optional Pi push; robust weight debounce.
- **Maintainability** – Smaller files, single place for config, documented protocol.
- **Compatibility** – Keep existing protocol (type 1, same cmd codes) so Pi and Master need no change, or version protocol explicitly if we change it.
- **Testability** – Logic testable without hardware where possible; clear inputs/outputs.

---

## 4. Proposed Architecture

### 4.1 Slave firmware (ESP32)

```
hydration_v2/
├── hydration_v2.ino       # setup(), loop(); non-blocking; processIncomingPackets(), time tick, state update
├── Config.h               # All #defines: pins, timeouts, thresholds, colors, Master MAC, HYDRATION_LOG
├── Log.h                  # Timestamped Serial: LOG_INFO(), LOG_WARN(); no-op when HYDRATION_LOG=0
├── Comms.h                # ESP-NOW init, send(), recv callback, packet struct (same protocol)
├── TimeSync.h             # rtcOffset, timeSynced, getHour(); tick() = request 0x30 if needed / timeout
├── Hardware.h             # Scale, LED, buzzer, RGB, NVM (same as v1; pins from Config)
└── StateMachine.h         # States, enterState(), update(), evaluateWeightChange(), processDrink()
```

- **hydration_v2.ino** – Include modules; call `TimeSync::waitWithTimeout()`, `StateMachine::update()`, `Comms::poll()` (or recv in callback and flag).
- **Config** – One place for CHECK_INTERVAL_MS, DRINK_MIN_ML, TIME_SYNC_TIMEOUT_MS, MASTER_MAC, etc.
- **Comms** – Same protocol; send(type, cmd, data); recv fills packet and sets flag; main loop calls “process packet” once per loop.
- **TimeSync** – Holds `rtcOffset`, `timeSynced`; `waitWithTimeout()` (blocking with timeout) or “tick” for non-blocking; `getHour()` for sleep window.
- **StateMachine** – Same states and transitions as current LogicManager; inputs: weight, time, presence; outputs: send 0x50/0x51/0x52/0x53/0x60/0x61 via Comms.

### 4.2 Pi controller

- **Keep** existing handlers (hydration, drink_celebration, bottle_alert); they already match the protocol.
- **Optional** – “Hydration v2” handler that speaks a new protocol if we ever add version byte or new commands; until then, keep v1 protocol.

### 4.3 Protocol

- **v2.0** – Keep current 6-byte type-1 protocol so Pi and Master are unchanged.
- **Future** – If we add version: e.g. first byte = type, second = version, then cmd + data; Pi can branch on version.

---

## 5. Module Breakdown (Slave)

| Module | Responsibility | Depends on |
|--------|----------------|------------|
| **Config** | All #defines and Master MAC | - |
| **Comms** | WiFi/ESP-NOW, send packet, recv callback, packet buffer | Config |
| **Hardware** | Scale read/tare, LED, buzzer, RGB, NVM load/save | Config |
| **TimeSync** | RTC offset, timeSynced, getHour(), waitWithTimeout() | Comms, Config |
| **StateMachine** | States, transitions, weight evaluation, send 0x50/0x60/… | Hardware, Comms, TimeSync, Config |
| **Main (.ino)** | setup(), loop(), processIncomingPackets(), call TimeSync + StateMachine | All |

### 5.1 processIncomingPackets()

- Called from main loop (and from time-sync loop while waiting).
- If recv flag set: switch on `cmd`; handle 0x10, 0x11, 0x12, 0x31, 0x41, 0x22, 0x20, 0x23; call Hardware or StateMachine or TimeSync as needed; clear flag.

### 5.2 Time sync (v2: non-blocking)

- **Non-blocking** – No blocking in setup(). In loop(): if !timeSynced and interval elapsed (e.g. 5s), send 0x30 once; when 0x31 received, set rtcOffset and timeSynced; after 60s give up and continue without time (getHour() returns 12, daily reset disabled). Best for stability and performance: no long block, loop keeps running.
- **One sync at boot** – Slave requests time until it gets 0x31 (or timeout). Pi pushes time once at startup (existing time push); no need to keep sending time after that – Pi and slave both have time from that one sync.

### 5.3 State machine

- Same states: MONITORING, WAIT_FOR_PRESENCE, REMINDER_PRE, REMINDER_ACTIVE, REMOVED_DRINKING, MISSING_ALERT, STABILIZING.
- Same triggers: weight thresholds, timeouts, presence response.
- Keep: drink confirmation (400 ms re-read), boot-without-bottle baseline clear, “no baseline” branch in evaluateWeightChange.

---

## 6. Scope for v2 (decided)

1. **New folder** – `hydration_v2/` (keep current `hydration/` as-is).
2. **Non-blocking time sync** – Request 0x30 from loop() until 0x31 received or 60s timeout; no blocking in setup(). Time once at boot; Pi startup time push is enough, no need to keep sending.
3. **Timestamped Serial logging** – One place (e.g. `Log.h`) with macros like `LOG_INFO("msg")` that print `[ms] tag: msg` so Serial Monitor shows order and timing. Intended for monitoring and debugging; **optional compile-time switch** (e.g. `#define HYDRATION_LOG 1`) so you can set to 0 and remove/disable logs for production. Same idea on Pi: optional timestamped log lines for hydration/alert flow; can disable later.
4. **Config in one file** – All timing, thresholds, pins in Config.h.
5. **Same protocol and behaviour** – No protocol change; logic matches v1 (states, drink confirm, boot-without-bottle, etc.).
6. **Performance** – Non-blocking loop; minimal delay(); log macros no-op when HYDRATION_LOG=0 to avoid Serial overhead in production.

---

## 6.1 Feature parity with v1 (all covered in v2)

| Feature | Config / Command | v2 behaviour |
|--------|------------------|--------------|
| **No-bottle alert** | `MISSING_TIMEOUT_MS` (180s), `CMD_ALERT_MISSING` (0x50) | Bottle off scale > 180s → send 0x50; red blink + buzzer after `BUZZER_START_DELAY_MS`. When bottle returns → send 0x51 (REPLACED). |
| **Drinking reminder** | `CHECK_INTERVAL_MS` (30m), `CMD_ALERT_REMINDER` (0x52) | Interval expired, no drink → request presence; if home → send 0x52; LED blink then buzzer (`LED_ALERT_DURATION`, `BLINK_INTERVAL_MS`). Lifting bottle or away → snooze; send 0x53 (STOPPED) when alert ends. |
| **Drink detected** | `DRINK_MIN_ML`, `DRINK_CONFIRM_MS`, `CMD_DRINK_DETECTED` (0x60) | Weight drop ≥ 50 ml (with 400 ms confirm at interval) → send 0x60 + 0x61; green flash. |
| **Refill detected** | `REFILL_MIN_ML` | Weight gain ≥ 100 ml → blue flash; no Pi command (same as v1). |
| **Presence** | `PRESENCE_TIMEOUT_MS`, `AWAY_CHECK_INTERVAL_MS` | Request 0x40; Pi replies 0x41; away → snooze; home → start reminder (0x52). Timeout 10s → snooze. |
| **Sleep window** | `SLEEP_START_HOUR`, `SLEEP_END_HOUR` | 23:00–10:00 no reminder; RGB sleep color; active reminder silenced if sleep starts. |
| **Daily reset** | Time from Pi (once at boot) | New day → daily total reset; send 0x61. |
| **Boot without bottle** | `THRESHOLD_WEIGHT` | Baseline cleared; when bottle placed, baseline set only (no false drink). |
| **Colors** | `COLOR_ALERT`, `COLOR_OK`, `COLOR_REFILL`, `COLOR_IDLE`, `COLOR_SLEEP` | Same IDs (1=red, 2=green, 3=blue, 8=idle, 7=sleep). |

All v1 config knobs live in `hydration_v2/Config.h`; behaviour matches v1.

---

## 7. Migration Path

1. **Phase 1** – Create `hydration_v2/` (or new folder) with the structure above; implement modules so behaviour matches v1 (same protocol, same logic).
2. **Phase 2** – Test v2 slave with current Pi and Master; confirm time sync, weight, alerts, drink detection, daily total.
3. **Phase 3** – Optionally remove or archive old `hydration/` and rename v2 to main hydration sketch; update README and SLAVES_AND_DASHBOARD.md if needed.
4. **Pi** – No change required for v2 if protocol is unchanged; keep time push and existing handlers.

---

## 8. Checklist Before Coding

- [x] Protocol: keep 6-byte type-1 as-is for v2.
- [x] Folder: new `hydration_v2/`.
- [x] Time sync: non-blocking in loop(); one sync at boot; Pi push once at startup.
- [x] Pins/board: same as current (Config.h).
- [x] Master MAC: in Config (or Comms) and match actual Master.
- [x] Logging: timestamped Serial via Log.h; `HYDRATION_LOG` 1=on, 0=off for production.

---

## 9. Next Step

After you review this plan:

1. Adjust goals, structure, or protocol if needed.
2. Confirm “Phase 1” scope (e.g. “same behaviour, new layout” only).
3. Then we can implement step by step (e.g. Config + Comms first, then TimeSync, then StateMachine, then main .ino).

If you tell me your choices (e.g. “non-blocking time sync”, “keep blocking”, “new folder name”), I can turn this into a concrete file-by-file implementation plan or start with a single module.
