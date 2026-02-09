/*
 * Hydration Slave v3
 *
 * 1. Boot → rainbow LED starts.
 * 2. Request time from Pi over ESP-NOW until we receive it (or timeout).
 * 3. Once time is synced: if NOT sleep time → print current weight to Serial periodically.
 * 4. Same ESP-NOW protocol as before (Pi/master unchanged).
 *
 * See ALGORITHM.md for the flow and config.
 */

#include "Config.h"
#include "Comms.h"
#include "Hardware.h"
#include "TimeSync.h"
#include <math.h>

HydrationHW hw;
Comms comms;
TimeSync timeSync;

// Simple missing-bottle state (no full state machine yet)
static bool bottleMissing = false;
static unsigned long missingSinceMs = 0;
static bool missingAlertActive = false;
static unsigned long alertStartMs = 0;
static unsigned long lastBlinkMs = 0;
static bool blinkOn = false;

// Drinking logic (baseline + reminder)
static float baselineWeight = 0.0f;
static bool baselineValid = false;
static unsigned long lastDrinkCheckMs = 0;
static bool drinkAlertActive = false;
static unsigned long drinkAlertStartMs = 0;

// Presence from Pi (HOME / AWAY) for drinking alerts
static bool isHome = true;
static bool waitingPresenceForDrink = false;

// Drinking alert blink state
static unsigned long drinkBlinkMs = 0;
static bool drinkBlinkOn = false;

// Stabilization after bottle placed back
static bool stabilizingAfterReturn = false;
static unsigned long stabilizeStartMs = 0;

// Daily total tracking
static float dailyTotalMl = 0.0f;
static int currentDayIndex = 0;
static bool dayInitialized = false;

void processIncomingPackets() {
  if (!packetReceived) return;
  packetReceived = false;

#if HYDRATION_LOG
  Serial.print("[");
  Serial.print(millis());
  Serial.print("] CMD 0x");
  Serial.print(incomingPacket.command, HEX);
  Serial.print(" data ");
  Serial.println(incomingPacket.data);
#endif

  switch (incomingPacket.command) {
  case CMD_REPORT_TIME: {
    uint32_t timestamp = incomingPacket.data;
    timeSync.setTimeFromPi(timestamp);
    break;
  }
  case CMD_GET_WEIGHT: {
    float w = hw.getWeight();
    comms.sendFloat(CMD_REPORT_WEIGHT, w);
    break;
  }
  case CMD_TARE:
    hw.tare();
    comms.sendFloat(CMD_REPORT_WEIGHT, 0.0f);
    break;
  case CMD_SET_LED:
    hw.setLed(incomingPacket.data > 0);
    break;
  case CMD_SET_BUZZER:
    hw.setBuzzer(incomingPacket.data > 0);
    break;
  case CMD_REPORT_PRESENCE: {
    bool home = (incomingPacket.data != 0);
    isHome = home;
#if HYDRATION_LOG
    Serial.print("[");
    Serial.print(millis());
    Serial.print("] PRESENCE ");
    Serial.println(isHome ? "HOME" : "AWAY");
#endif
    if (waitingPresenceForDrink) {
      waitingPresenceForDrink = false;
      if (isHome && !drinkAlertActive) {
        drinkAlertActive = true;
        drinkAlertStartMs = millis();
        comms.send(CMD_ALERT_REMINDER, 0);
      } else {
        // User is away -> snooze (no alert). Next interval will re-check.
      }
    }
    break;
  }
  default:
    break;
  }
}

void setup() {
  Serial.begin(115200);
#if HYDRATION_LOG
  Serial.println("[v3] Booting...");
#endif

  comms.begin();
  delay(100);
  hw.begin();
  timeSync.begin();

  // Load last baseline weight from NVM, if available
  if (hw.loadBaseline(&baselineWeight)) {
    baselineValid = true;
#if HYDRATION_LOG
    Serial.print("[v3] Loaded baseline from NVM: ");
    Serial.println(baselineWeight);
#endif
  } else {
    baselineValid = false;
#if HYDRATION_LOG
    Serial.println("[v3] No baseline in NVM - will trigger first drinking alert when bottle present.");
#endif
  }

  // Load daily total from NVM
  hw.loadTotals(&dailyTotalMl, &currentDayIndex);
#if HYDRATION_LOG
  Serial.print("[v3] Loaded daily total from NVM: ");
  Serial.print(dailyTotalMl);
  Serial.print(" ml, day index ");
  Serial.println(currentDayIndex);
#endif

#if HYDRATION_LOG
  Serial.println("[v3] Rainbow on. Requesting time from Pi...");
#endif
}

void loop() {
  processIncomingPackets();
  timeSync.tick(comms);

  if (!timeSync.isSynced()) {
    hw.animateRainbow(30);
    return;
  }

  static bool rainbowStopped = false;
  if (!rainbowStopped) {
    rainbowStopped = true;
    hw.setRgb(0, 0, 0);
  }

  // Time synced: check sleep window (Config.h: SLEEP_START_HOUR, SLEEP_END_HOUR)
  int hour = timeSync.getHour();
  bool isSleepTime = (hour >= SLEEP_START_HOUR || hour < SLEEP_END_HOUR);

  unsigned long now = millis();

  // --- Day tracking for daily total ---
  if (timeSync.isSynced()) {
    int day = (int)timeSync.getDay();
    if (!dayInitialized) {
      dayInitialized = true;
      if (currentDayIndex != day) {
        currentDayIndex = day;
        dailyTotalMl = 0.0f;
        hw.saveTotals(dailyTotalMl, currentDayIndex);
        comms.sendFloat(CMD_DAILY_TOTAL, dailyTotalMl);
#if HYDRATION_LOG
        Serial.print("[v3] New day detected (init) -> reset daily total to ");
        Serial.println(dailyTotalMl);
#endif
      }
    } else if (day != currentDayIndex) {
      currentDayIndex = day;
      dailyTotalMl = 0.0f;
      hw.saveTotals(dailyTotalMl, currentDayIndex);
      comms.sendFloat(CMD_DAILY_TOTAL, dailyTotalMl);
#if HYDRATION_LOG
      Serial.print("[v3] New day detected -> reset daily total to ");
      Serial.println(dailyTotalMl);
#endif
    }
  }

  // --- Bottle present / missing detection + reporting ---
  static unsigned long lastSampleMs = 0;
  if (now - lastSampleMs >= WEIGHT_PRINT_INTERVAL_MS) {
    lastSampleMs = now;

    float w = hw.getWeight();

    // Always send weight to Pi for dashboard
    comms.sendFloat(CMD_REPORT_WEIGHT, w);

    // Detect missing / placed-back transitions
    if (w < THRESHOLD_WEIGHT) {
      if (!bottleMissing) {
        bottleMissing = true;
        missingSinceMs = now;
        missingAlertActive = false;
        lastBlinkMs = 0;
        blinkOn = false;
        hw.setBuzzer(false);
        hw.setLed(false);  // stop white light immediately when bottle removed

        // When bottle is lifted, stop any active drinking alert
        if (drinkAlertActive) {
          drinkAlertActive = false;
          comms.send(CMD_ALERT_STOPPED, 0);
        }
      }
    } else {
      if (bottleMissing) {
        bottleMissing = false;
        hw.setBuzzer(false);
        missingAlertActive = false;
        // Notify Pi: bottle placed back
        comms.send(CMD_ALERT_REPLACED, 0);
        // We will evaluate drink/refill vs baseline later (on interval); no immediate change here
        stabilizingAfterReturn = true;
        stabilizeStartMs = now;
#if HYDRATION_LOG
        Serial.println("[v3] Bottle returned - starting stabilization window.");
#endif
      }
    }

    // Only print weight to Serial when not in sleep window
    if (!isSleepTime) {
      Serial.print("[");
      Serial.print(now);
      Serial.print("] weight ");
      Serial.println(w);
    }
  }

  // --- Drinking logic (daytime only, bottle present, not missing) ---
  if (!isSleepTime && !bottleMissing) {
    // Wait for stabilization after bottle returned
    if (stabilizingAfterReturn) {
      if (now - stabilizeStartMs < STABILIZATION_MS) {
#if HYDRATION_LOG
        Serial.println("[v3] Stabilizing after return... waiting before drink/refill evaluation.");
#endif
        return;
      } else {
        stabilizingAfterReturn = false;
#if HYDRATION_LOG
        Serial.println("[v3] Stabilization complete - evaluating drink/refill vs baseline.");
#endif
      }
    }

    float wCurrent = hw.getWeight(); // fresh read for decision

    // First run: no baseline stored yet -> set baseline and trigger drinking alert
    if (!baselineValid) {
      baselineWeight = wCurrent;
      baselineValid = true;
      hw.saveBaseline(baselineWeight);
      // Before starting alert, check presence with Pi
      waitingPresenceForDrink = true;
#if HYDRATION_LOG
      Serial.println("[v3] First baseline set - requesting presence before drinking alert.");
#endif
      comms.send(CMD_REQUEST_PRESENCE, 0);
    } else if (now - lastDrinkCheckMs >= DRINK_CHECK_INTERVAL_MS) {
      lastDrinkCheckMs = now;
      float diff = baselineWeight - wCurrent; // +ve = user drank, -ve = refill
      float absDiff = fabs(diff);

      if (diff >= DRINK_MIN_DELTA) {
        // User drank
        comms.sendFloat(CMD_DRINK_DETECTED, diff);
        dailyTotalMl += diff;
        hw.saveTotals(dailyTotalMl, currentDayIndex);
        comms.sendFloat(CMD_DAILY_TOTAL, dailyTotalMl);
#if HYDRATION_LOG
        Serial.print("[v3] User drank ");
        Serial.print(diff);
        Serial.print(" ml. Daily total now ");
        Serial.println(dailyTotalMl);
#endif
        baselineWeight = wCurrent;
        hw.saveBaseline(baselineWeight);
        drinkAlertActive = false;
        drinkBlinkMs = 0;
        drinkBlinkOn = false;
        comms.send(CMD_ALERT_STOPPED, 0);
      } else if (diff <= -REFILL_MIN_DELTA) {
        // Bottle refilled
        // Optional: define a REFILL command; for now we just log via Serial and update baseline
#if HYDRATION_LOG
        Serial.print("[v3] Bottle refilled by ");
        Serial.print(-diff);
        Serial.println(" ml - updating baseline.");
#endif
        baselineWeight = wCurrent;
        hw.saveBaseline(baselineWeight);
        drinkAlertActive = false;
        drinkBlinkMs = 0;
        drinkBlinkOn = false;
        comms.send(CMD_ALERT_STOPPED, 0);
      } else if (absDiff <= DRINK_NOISE_THRESHOLD) {
        // No significant change -> request presence; only remind if user is HOME
        if (!drinkAlertActive && !waitingPresenceForDrink) {
          waitingPresenceForDrink = true;
#if HYDRATION_LOG
          Serial.println("[v3] No significant drink detected - requesting presence for reminder.");
#endif
          comms.send(CMD_REQUEST_PRESENCE, 0);
        }
      }
      // If small but not inside noise band, we keep baseline and do nothing (micro-sips)
    }
  }

  // --- Visuals & buzzer based on state ---
  if (bottleMissing) {
    // Wait for configured delay before starting the \"no bottle\" alert
    if (!missingAlertActive) {
      if (now - missingSinceMs >= MISSING_ALERT_DELAY_MS) {
        missingAlertActive = true;
        alertStartMs = now;
        lastBlinkMs = 0;
        blinkOn = false;
        // Now trigger the missing bottle alert to Pi
        comms.send(CMD_ALERT_MISSING, 0);
      } else {
        // Before alert starts, just show normal day/sleep colour and no buzzer
        hw.setBuzzer(false);
        if (isSleepTime) {
          hw.setRgb(COLOR_SLEEP_R, COLOR_SLEEP_G, COLOR_SLEEP_B);
        } else {
          hw.setRgb(COLOR_DAY_R, COLOR_DAY_G, COLOR_DAY_B);
        }
        return;
      }
    }

    // Alert is active: flash missing colour
    if (now - lastBlinkMs >= BLINK_INTERVAL_MS) {
      lastBlinkMs = now;
      blinkOn = !blinkOn;
      if (blinkOn) {
        hw.setRgb(COLOR_MISSING_R, COLOR_MISSING_G, COLOR_MISSING_B);
      } else {
        hw.setRgb(0, 0, 0);
      }
    }

    // After additional delay from alert start, join with buzzer
    if (now - alertStartMs >= MISSING_BUZZER_DELAY_MS) {
      hw.setBuzzer(blinkOn);
    } else {
      hw.setBuzzer(false);
    }
  } else {
    // No missing bottle: buzzer off, color by sleep/day time
    hw.setBuzzer(false);
    if (isSleepTime) {
      hw.setRgb(COLOR_SLEEP_R, COLOR_SLEEP_G, COLOR_SLEEP_B);
    } else {
      // Daytime: if a drinking alert is active, show its colour and after some time add buzzer
      if (drinkAlertActive) {
        // Drinking alert: cyan + white LED; buzzer joins for a limited window
        if (now - drinkBlinkMs >= BLINK_INTERVAL_MS) {
          drinkBlinkMs = now;
          drinkBlinkOn = !drinkBlinkOn;
        }

        if (drinkBlinkOn) {
          hw.setRgb(COLOR_DRINK_ALERT_R, COLOR_DRINK_ALERT_G, COLOR_DRINK_ALERT_B);
          hw.setLed(true);
        } else {
          hw.setRgb(0, 0, 0);
          hw.setLed(false);
        }

        unsigned long sinceStart = now - drinkAlertStartMs;
        if (sinceStart >= DRINK_ALERT_BUZZER_DELAY_MS &&
            sinceStart <  DRINK_ALERT_BUZZER_DELAY_MS + DRINK_ALERT_BUZZER_WINDOW_MS &&
            drinkBlinkOn) {
          hw.setBuzzer(true);   // buzzer follows the blink, for 10s window
        } else {
          hw.setBuzzer(false);
        }
      } else {
        hw.setRgb(COLOR_DAY_R, COLOR_DAY_G, COLOR_DAY_B);
        hw.setLed(false);
      }
    }
  }
}
