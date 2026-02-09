/*
 * Hydration Slave v3 - Clean rewrite
 *
 * Flow:
 *   Boot -> Rainbow until time sync from Pi
 *   After sync: day/sleep color, weight sampling, missing bottle alert,
 *               drinking interval check with presence gate, stabilization
 *               after bottle return with immediate evaluation.
 *
 * Key rule: the visual section at the bottom ALWAYS runs. No early returns
 *           after time sync so the LED/buzzer/RGB are never left in a stale state.
 */

#include "Config.h"
#include "Comms.h"
#include "Hardware.h"
#include "TimeSync.h"
#include <math.h>

// ==================== MODULES ====================
HydrationHW hw;
Comms comms;
TimeSync timeSync;

// ==================== STATE ====================
// --- Missing bottle ---
static bool bottleMissing        = false;
static unsigned long missingStartMs   = 0;
static bool missingAlertActive   = false;
static unsigned long missingAlertMs   = 0;
static unsigned long missingBlinkMs   = 0;
static bool missingBlinkOn       = false;

// --- Stabilization (after bottle return) ---
static bool stabilizing          = false;
static unsigned long stabilizeStartMs = 0;

// --- Drinking baseline & interval ---
static float baselineWeight      = 0.0f;
static bool  baselineValid       = false;
static unsigned long lastDrinkCheckMs = 0;

// --- Drinking alert ---
static bool drinkAlertActive     = false;
static unsigned long drinkAlertStartMs = 0;
static unsigned long drinkBlinkMs = 0;
static bool drinkBlinkOn         = false;

// --- Presence ---
static bool isHome               = true;
static bool waitingPresence      = false;

// --- Daily totals ---
static float dailyTotalMl        = 0.0f;
static int   currentDayIndex     = 0;
static bool  dayInitialized      = false;

// ==================== HELPERS ====================

void LOG(const char *msg) {
#if HYDRATION_LOG
  Serial.print("["); Serial.print(millis()); Serial.print("] ");
  Serial.println(msg);
#endif
}

void LOG2(const char *msg, float val) {
#if HYDRATION_LOG
  Serial.print("["); Serial.print(millis()); Serial.print("] ");
  Serial.print(msg); Serial.println(val);
#endif
}

void LOG2S(const char *msg, const char *val) {
#if HYDRATION_LOG
  Serial.print("["); Serial.print(millis()); Serial.print("] ");
  Serial.print(msg); Serial.println(val);
#endif
}

// --- Start drinking alert (called when presence confirmed HOME) ---
void startDrinkAlert() {
  if (drinkAlertActive) return;
  drinkAlertActive   = true;
  drinkAlertStartMs  = millis();
  drinkBlinkMs       = millis();
  drinkBlinkOn       = false;
  comms.send(CMD_ALERT_REMINDER, 0);
  LOG("DRINK ALERT -> STARTED (user HOME).");
}

// --- Stop drinking alert ---
void stopDrinkAlert() {
  if (!drinkAlertActive) return;
  drinkAlertActive = false;
  drinkBlinkMs     = 0;
  drinkBlinkOn     = false;
  hw.setBuzzer(false);
  hw.setLed(false);
  comms.send(CMD_ALERT_STOPPED, 0);
  LOG("DRINK ALERT -> STOPPED.");
}

// --- Record a confirmed drink ---
void recordDrink(float amount, float newWeight) {
  LOG2("RESULT: User drank ", amount);
  LOG2(" ml. New baseline: ", newWeight);
  dailyTotalMl += amount;
  baselineWeight = newWeight;
  hw.saveBaseline(baselineWeight);
  hw.saveTotals(dailyTotalMl, currentDayIndex);
  comms.sendFloat(CMD_DRINK_DETECTED, amount);
  comms.sendFloat(CMD_DAILY_TOTAL, dailyTotalMl);
  LOG2("Daily total now: ", dailyTotalMl);
  lastDrinkCheckMs = millis();  // reset interval timer
  stopDrinkAlert();
}

// --- Record a confirmed refill ---
void recordRefill(float amount, float newWeight) {
  LOG2("RESULT: Bottle refilled +", amount);
  LOG2(" ml. New baseline: ", newWeight);
  baselineWeight = newWeight;
  hw.saveBaseline(baselineWeight);
  lastDrinkCheckMs = millis();  // reset interval timer
  stopDrinkAlert();
}

// --- Evaluate weight vs baseline (called after stabilization OR at interval) ---
//     Returns: true if drink or refill was detected (interval reset already done)
//
//     Logic matches old LogicManager.h: anything that is NOT a confirmed drink
//     or confirmed refill triggers a presence check → drinking reminder.
//     Baseline is preserved on "no change" so small sips accumulate until
//     they cross DRINK_MIN_DELTA (just like old code line 314-321).
bool evaluateWeight(float currentWeight) {
  if (!baselineValid) {
    // First time: set baseline, request presence for first alert
    baselineWeight = currentWeight;
    baselineValid  = true;
    hw.saveBaseline(baselineWeight);
    lastDrinkCheckMs = millis();
    LOG2("First baseline set: ", baselineWeight);
    if (!drinkAlertActive && !waitingPresence) {
      waitingPresence = true;
      comms.send(CMD_REQUEST_PRESENCE, 0);
      LOG("Requesting presence for first drinking alert.");
    }
    return false;
  }

  float diff = baselineWeight - currentWeight;  // +ve = drank, -ve = refill

  LOG2("Evaluate: baseline=", baselineWeight);
  LOG2("          current =", currentWeight);
  LOG2("          diff    =", diff);

  // --- Confirmed drink ---
  if (diff >= DRINK_MIN_DELTA) {
    recordDrink(diff, currentWeight);
    return true;
  }
  // --- Confirmed refill ---
  if (diff <= -REFILL_MIN_DELTA) {
    recordRefill(-diff, currentWeight);
    return true;
  }

  // --- Not a confirmed drink or refill (matches old LogicManager line 314-321) ---
  // Baseline preserved: small sips accumulate until DRINK_MIN_DELTA crossed.
  // Request presence for drinking reminder (no mercy).
  LOG2("No confirmed drink/refill (diff=", diff);
  LOG(" ). Baseline preserved. Requesting presence for reminder.");
  lastDrinkCheckMs = millis();  // CRITICAL: prevent tight re-evaluation loop
  if (!drinkAlertActive && !waitingPresence) {
    waitingPresence = true;
    comms.send(CMD_REQUEST_PRESENCE, 0);
    LOG("Sent CMD_REQUEST_PRESENCE to Pi.");
  }
  return false;
}

// ==================== PACKET HANDLER ====================

void processIncomingPackets() {
  if (!packetReceived) return;
  packetReceived = false;

#if HYDRATION_LOG
  Serial.print("["); Serial.print(millis());
  Serial.print("] CMD 0x"); Serial.print(incomingPacket.command, HEX);
  Serial.print(" data "); Serial.println(incomingPacket.data);
#endif

  switch (incomingPacket.command) {
  case CMD_REPORT_TIME:
    timeSync.setTimeFromPi(incomingPacket.data);
    break;

  case CMD_GET_WEIGHT:
    comms.sendFloat(CMD_REPORT_WEIGHT, hw.getWeight());
    break;

  case CMD_TARE:
    hw.tare();
    comms.sendFloat(CMD_REPORT_WEIGHT, 0.0f);
    LOG("TARE done.");
    break;

  case CMD_SET_LED:
    hw.setLed(incomingPacket.data > 0);
    break;

  case CMD_SET_BUZZER:
    hw.setBuzzer(incomingPacket.data > 0);
    break;

  case CMD_REQUEST_DAILY_TOTAL:
    comms.sendFloat(CMD_DAILY_TOTAL, dailyTotalMl);
    break;

  case CMD_REPORT_PRESENCE: {
    bool home = (incomingPacket.data != 0);
    isHome = home;
    LOG2S("PRESENCE: ", isHome ? "HOME" : "AWAY");

    if (waitingPresence) {
      waitingPresence = false;
      if (isHome) {
        startDrinkAlert();
      } else {
        LOG("User AWAY -> skipping drinking alert (will retry next interval).");
      }
    }
    break;
  }

  default:
    break;
  }
}

// ==================== SETUP ====================

void setup() {
  Serial.begin(115200);
  LOG("v3 Booting...");

  comms.begin();
  delay(100);
  hw.begin();
  timeSync.begin();

  // Load baseline from NVM
  if (hw.loadBaseline(&baselineWeight)) {
    baselineValid = true;
    LOG2("Loaded baseline from NVM: ", baselineWeight);
  } else {
    baselineValid = false;
    LOG("No baseline in NVM. Will set on first bottle detection.");
  }

  // Load daily total from NVM
  hw.loadTotals(&dailyTotalMl, &currentDayIndex);
  LOG2("Loaded daily total: ", dailyTotalMl);

  LOG("Rainbow on. Waiting for time sync from Pi...");
}

// ==================== MAIN LOOP ====================

void loop() {
  // --- Always: process packets and time sync ---
  processIncomingPackets();
  timeSync.tick(comms);

  // --- Before time sync: rainbow and nothing else ---
  if (!timeSync.isSynced()) {
    hw.animateRainbow(30);
    return;  // only early return in the whole loop
  }

  // --- One-time: stop rainbow after sync ---
  static bool rainbowDone = false;
  if (!rainbowDone) {
    rainbowDone = true;
    hw.setRgb(0, 0, 0);
    hw.setLed(false);
    hw.setBuzzer(false);
    LOG("Time synced. Rainbow off.");
  }

  // --- Compute time state ---
  int hour = timeSync.getHour();
  bool isSleep = (hour >= SLEEP_START_HOUR || hour < SLEEP_END_HOUR);
  unsigned long now = millis();

  // --- Day tracking (reset daily total at midnight) ---
  {
    int day = (int)timeSync.getDay();
    if (!dayInitialized) {
      dayInitialized = true;
      if (currentDayIndex != day) {
        currentDayIndex = day;
        dailyTotalMl = 0.0f;
        hw.saveTotals(dailyTotalMl, currentDayIndex);
        comms.sendFloat(CMD_DAILY_TOTAL, dailyTotalMl);
        LOG("New day -> daily total reset.");
      }
    } else if (day != currentDayIndex) {
      currentDayIndex = day;
      dailyTotalMl = 0.0f;
      hw.saveTotals(dailyTotalMl, currentDayIndex);
      comms.sendFloat(CMD_DAILY_TOTAL, dailyTotalMl);
      LOG("New day -> daily total reset.");
    }
  }

  // ====================================================
  // SECTION 1: Sample weight, send to Pi, print to Serial
  // ====================================================
  static unsigned long lastSampleMs = 0;
  if (now - lastSampleMs >= WEIGHT_PRINT_INTERVAL_MS) {
    lastSampleMs = now;
    float w = hw.getWeight();
    comms.sendFloat(CMD_REPORT_WEIGHT, w);
    if (!isSleep) {
      Serial.print("["); Serial.print(now);
      Serial.print("] weight "); Serial.println(w);
    }
  }

  // ====================================================
  // SECTION 2: Bottle missing / present transitions
  // ====================================================
  float currentW = hw.getWeight();

  if (currentW < THRESHOLD_WEIGHT) {
    // --- Bottle is OFF the scale ---
    if (!bottleMissing) {
      bottleMissing      = true;
      missingStartMs     = now;
      missingAlertActive = false;
      missingBlinkMs     = 0;
      missingBlinkOn     = false;
      stabilizing        = false;
      LOG("Bottle LIFTED.");

      // Stop any active drinking alert immediately
      if (drinkAlertActive) {
        stopDrinkAlert();
      }
    }
  } else {
    // --- Bottle is ON the scale ---
    if (bottleMissing) {
      bottleMissing      = false;
      missingAlertActive = false;
      stabilizing        = true;
      stabilizeStartMs   = now;
      comms.send(CMD_ALERT_REPLACED, 0);
      LOG("Bottle RETURNED. Stabilizing...");
    }
  }

  // ====================================================
  // SECTION 3: Stabilization (wait, then evaluate immediately)
  // ====================================================
  // Note: we do NOT return here. The visual section below still runs.
  if (stabilizing) {
    if (now - stabilizeStartMs >= STABILIZATION_MS) {
      stabilizing = false;
      float stableW = hw.getWeight();
      LOG2("Stabilized at: ", stableW);
      LOG("Evaluating drink/refill vs baseline...");
      evaluateWeight(stableW);
      // After evaluation, lastDrinkCheckMs is reset if drink/refill detected.
      // If no change, lastDrinkCheckMs is NOT reset, so the interval check
      // will fire again soon (matching old v1 behaviour: no mercy).
    }
    // While stabilizing, skip interval check but DO run visuals below.
  }

  // ====================================================
  // SECTION 4: Interval drinking logic (daytime, bottle present, not stabilizing)
  // ====================================================
  // Guards match old LogicManager state machine: evaluation only runs in
  // "MONITORING" equivalent (not during alert, not waiting for presence reply).
  else if (!isSleep && !bottleMissing && !drinkAlertActive && !waitingPresence) {
    float wNow = hw.getWeight();

    if (!baselineValid) {
      // First time ever: set baseline and request presence for first alert
      evaluateWeight(wNow);
    } else if (now - lastDrinkCheckMs >= DRINK_CHECK_INTERVAL_MS) {
      LOG("Interval expired. Evaluating...");
      evaluateWeight(wNow);
    }
  }

  // ====================================================
  // SECTION 5: VISUALS (always runs, never skipped)
  // ====================================================
  // Priority: missing alert > stabilizing > drinking alert > sleep > day

  if (bottleMissing) {
    // ----- Missing bottle path -----
    if (!missingAlertActive) {
      if (now - missingStartMs >= MISSING_ALERT_DELAY_MS) {
        missingAlertActive = true;
        missingAlertMs     = now;
        missingBlinkMs     = 0;
        missingBlinkOn     = false;
        comms.send(CMD_ALERT_MISSING, 0);
        LOG("MISSING ALERT -> STARTED (sending to Pi).");
      } else {
        // Before alert delay: show day/sleep color, buzzer off
        hw.setBuzzer(false);
        hw.setLed(false);
        if (isSleep) {
          hw.setRgb(COLOR_SLEEP_R, COLOR_SLEEP_G, COLOR_SLEEP_B);
        } else {
          hw.setRgb(COLOR_DAY_R, COLOR_DAY_G, COLOR_DAY_B);
        }
      }
    }

    if (missingAlertActive) {
      // Flash red
      if (now - missingBlinkMs >= BLINK_INTERVAL_MS) {
        missingBlinkMs = now;
        missingBlinkOn = !missingBlinkOn;
      }
      if (missingBlinkOn) {
        hw.setRgb(COLOR_MISSING_R, COLOR_MISSING_G, COLOR_MISSING_B);
        hw.setLed(true);
      } else {
        hw.setRgb(0, 0, 0);
        hw.setLed(false);
      }
      // Buzzer joins after extra delay
      if (now - missingAlertMs >= MISSING_BUZZER_DELAY_MS) {
        hw.setBuzzer(missingBlinkOn);
      } else {
        hw.setBuzzer(false);
      }
    }

  } else if (stabilizing) {
    // ----- Stabilizing: show day/sleep color, everything else off -----
    hw.setBuzzer(false);
    hw.setLed(false);
    if (isSleep) {
      hw.setRgb(COLOR_SLEEP_R, COLOR_SLEEP_G, COLOR_SLEEP_B);
    } else {
      hw.setRgb(COLOR_DAY_R, COLOR_DAY_G, COLOR_DAY_B);
    }

  } else if (drinkAlertActive && !isSleep) {
    // ----- Drinking alert: cyan + white blink, buzzer joins after delay -----
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

    unsigned long since = now - drinkAlertStartMs;
    if (since >= DRINK_ALERT_BUZZER_DELAY_MS &&
        since <  DRINK_ALERT_BUZZER_DELAY_MS + DRINK_ALERT_BUZZER_WINDOW_MS) {
      hw.setBuzzer(drinkBlinkOn);  // buzzer follows blink during window
    } else {
      hw.setBuzzer(false);
    }

  } else {
    // ----- Normal: day or sleep color -----
    hw.setBuzzer(false);
    hw.setLed(false);
    if (isSleep) {
      hw.setRgb(COLOR_SLEEP_R, COLOR_SLEEP_G, COLOR_SLEEP_B);
    } else {
      hw.setRgb(COLOR_DAY_R, COLOR_DAY_G, COLOR_DAY_B);
    }
  }
}
