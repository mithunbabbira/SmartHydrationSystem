/*
 * Hydration Slave v3 - Stability hardened
 *
 * Flow:
 *   Boot -> Rainbow until time sync or timeout
 *   Startup init -> robust bottle/baseline bootstrap
 *   Runtime -> day/sleep color, weight sampling, missing bottle alert,
 *              interval/stabilization evaluation, presence-gated reminders.
 *
 * Key rule: after sync/timeout the visual section ALWAYS runs.
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
static bool bottleMissing              = false;
static unsigned long missingStartMs    = 0;
static bool missingAlertActive         = false;
static unsigned long missingAlertMs    = 0;
static unsigned long missingBlinkMs    = 0;
static bool missingBlinkOn             = false;

// --- Bottle state confirmation (jitter filter) ---
static unsigned long bottleSampleMs    = 0;
static int missingConfirmCount         = 0;
static int presentConfirmCount         = 0;

// --- Stabilization (after bottle return) ---
static bool stabilizing                = false;
static unsigned long stabilizeStartMs  = 0;

// --- Drinking baseline & interval ---
static float baselineWeight            = 0.0f;
static bool baselineValid              = false;
static unsigned long lastDrinkCheckMs  = 0;

// --- Drinking alert ---
static bool drinkAlertActive           = false;
static unsigned long drinkAlertStartMs = 0;
static unsigned long drinkBlinkMs      = 0;
static bool drinkBlinkOn               = false;
static bool resumeReminderAfterReturn  = false;

// --- Presence ---
static bool isHome                     = true;
static bool waitingPresence            = false;
static unsigned long waitingPresenceSinceMs = 0;
static unsigned long presenceRetryNotBeforeMs = 0;
static bool forceImmediateEvaluation   = false;

// --- Startup ---
static bool startupInitDone            = false;
static bool fallbackTimeLogged         = false;

// --- Daily totals ---
static float dailyTotalMl              = 0.0f;
static int currentDayIndex             = 0;
static bool dayInitialized             = false;

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

// Safe target-time comparison across millis() overflow.
bool timeReached(unsigned long now, unsigned long target) {
  return ((long)(now - target) >= 0);
}

float median3(float a, float b, float c) {
  if ((a <= b && b <= c) || (c <= b && b <= a)) return b;
  if ((b <= a && a <= c) || (c <= a && a <= b)) return a;
  return c;
}

float readConfirmedWeight() {
  float w1 = hw.getWeight();
  delay(WEIGHT_CONFIRM_DELAY_MS);
  float w2 = hw.getWeight();

  float drift = fabsf(w2 - w1);
  if (drift <= WEIGHT_CONFIRM_MAX_DRIFT_G) {
    float avg = (w1 + w2) * 0.5f;
    LOG2("Confirmed weight(avg): ", avg);
    return avg;
  }

  // High drift: take a third sample and use median.
  delay(WEIGHT_CONFIRM_DELAY_MS);
  float w3 = hw.getWeight();
  float med = median3(w1, w2, w3);
  LOG2("Confirmed weight(median): ", med);
  return med;
}

float sampleStableWeight() {
  float sum = 0.0f;
  for (int i = 0; i < BOTTLE_CONFIRM_SAMPLES; i++) {
    sum += hw.getWeight();
    if (i + 1 < BOTTLE_CONFIRM_SAMPLES) {
      delay(BOTTLE_SAMPLE_INTERVAL_MS);
    }
  }
  float avg = sum / (float)BOTTLE_CONFIRM_SAMPLES;
  LOG2("Startup stable weight: ", avg);
  return avg;
}

void requestPresenceIfNeeded(const char *logMsg) {
  if (drinkAlertActive || waitingPresence) return;
  waitingPresence = true;
  waitingPresenceSinceMs = millis();
  comms.send(CMD_REQUEST_PRESENCE, 0);
  if (logMsg) LOG(logMsg);
}

void handlePresenceTimeout(unsigned long now) {
  if (!waitingPresence) return;
  if (now - waitingPresenceSinceMs < PRESENCE_REPLY_TIMEOUT_MS) return;

  waitingPresence = false;
  waitingPresenceSinceMs = 0;
  presenceRetryNotBeforeMs = now + PRESENCE_RETRY_AFTER_TIMEOUT_MS;
  LOG("Presence reply timeout. Will retry reminder check later.");
}

// --- Start drinking alert (called when presence confirmed HOME) ---
void startDrinkAlert() {
  if (drinkAlertActive) return;
  drinkAlertActive   = true;
  drinkAlertStartMs  = millis();
  drinkBlinkMs       = millis();
  drinkBlinkOn       = false;
  resumeReminderAfterReturn = false;
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

  lastDrinkCheckMs = millis();
  presenceRetryNotBeforeMs = 0;
  waitingPresence = false;
  waitingPresenceSinceMs = 0;
  resumeReminderAfterReturn = false;
  stopDrinkAlert();
}

// --- Record a confirmed refill ---
void recordRefill(float amount, float newWeight) {
  LOG2("RESULT: Bottle refilled +", amount);
  LOG2(" ml. New baseline: ", newWeight);
  baselineWeight = newWeight;
  hw.saveBaseline(baselineWeight);

  lastDrinkCheckMs = millis();
  presenceRetryNotBeforeMs = 0;
  waitingPresence = false;
  waitingPresenceSinceMs = 0;
  resumeReminderAfterReturn = false;
  stopDrinkAlert();
}

// --- Evaluate weight vs baseline ---
// allowReminderOnNoChange:
//   true  -> normal interval path (can request presence/reminder)
//   false -> stabilization path after bottle return (no immediate reminder)
bool evaluateWeight(float currentWeightRaw, bool allowReminderOnNoChange = true) {
  forceImmediateEvaluation = false;
  float currentWeight = readConfirmedWeight();
  LOG2("Evaluate raw=", currentWeightRaw);

  if (!baselineValid) {
    baselineWeight = currentWeight;
    baselineValid  = true;
    hw.saveBaseline(baselineWeight);
    lastDrinkCheckMs = millis();
    LOG2("First baseline set: ", baselineWeight);
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

  // --- No confirmed drink/refill ---
  LOG2("No confirmed drink/refill (diff=", diff);
  if (!allowReminderOnNoChange) {
    if (resumeReminderAfterReturn) {
      LOG(" ). Baseline preserved. Resuming active reminder after bottle return.");
      lastDrinkCheckMs = millis();
      if (isHome) {
        startDrinkAlert();
      } else {
        LOG("Last known presence is AWAY. Reminder not resumed.");
      }
      resumeReminderAfterReturn = false;
      return false;
    }
    LOG(" ). Baseline preserved. Stabilization path -> no immediate reminder.");
    return false;
  }

  LOG(" ). Baseline preserved. Requesting presence for reminder.");
  lastDrinkCheckMs = millis();
  requestPresenceIfNeeded("Sent CMD_REQUEST_PRESENCE to Pi.");
  return false;
}

void updateBottlePresence(unsigned long now, float currentW) {
  if (!timeReached(now, bottleSampleMs + BOTTLE_SAMPLE_INTERVAL_MS)) return;
  bottleSampleMs = now;

  const float presentThreshold = THRESHOLD_WEIGHT + BOTTLE_HYSTERESIS_G;
  bool missingCandidate = (currentW < THRESHOLD_WEIGHT);
  bool presentCandidate = (currentW > presentThreshold);

  if (!bottleMissing) {
    if (missingCandidate) {
      missingConfirmCount++;
      if (missingConfirmCount >= BOTTLE_CONFIRM_SAMPLES) {
        bottleMissing      = true;
        missingStartMs     = now;
        missingAlertActive = false;
        missingBlinkMs     = 0;
        missingBlinkOn     = false;
        stabilizing        = false;
        waitingPresence    = false;
        waitingPresenceSinceMs = 0;
        presenceRetryNotBeforeMs = 0;
        presentConfirmCount = 0;
        missingConfirmCount = 0;
        LOG("Bottle LIFTED (confirmed).");

        if (drinkAlertActive) {
          resumeReminderAfterReturn = true;
          stopDrinkAlert();
        } else {
          resumeReminderAfterReturn = false;
        }
      }
    } else {
      missingConfirmCount = 0;
    }
  } else {
    if (presentCandidate) {
      presentConfirmCount++;
      if (presentConfirmCount >= BOTTLE_CONFIRM_SAMPLES) {
        bottleMissing      = false;
        missingAlertActive = false;
        stabilizing        = true;
        stabilizeStartMs   = now;
        missingConfirmCount = 0;
        presentConfirmCount = 0;
        comms.send(CMD_ALERT_REPLACED, 0);
        LOG("Bottle RETURNED (confirmed). Stabilizing...");
      }
    } else {
      presentConfirmCount = 0;
    }
  }
}

void runStartupInit(unsigned long now) {
  if (startupInitDone) return;

  float stableW = sampleStableWeight();
  bool missingAtBoot = (stableW < THRESHOLD_WEIGHT);
  bool hadBaseline = baselineValid;

  missingConfirmCount = 0;
  presentConfirmCount = 0;
  bottleSampleMs = now;
  missingAlertActive = false;
  missingBlinkMs = 0;
  missingBlinkOn = false;
  waitingPresence = false;
  waitingPresenceSinceMs = 0;
  presenceRetryNotBeforeMs = 0;
  resumeReminderAfterReturn = false;
  stabilizing = false;

  if (missingAtBoot) {
    bottleMissing = true;
    missingStartMs = now;
    baselineValid = false;
    baselineWeight = 0.0f;
    LOG("Startup: bottle missing -> baseline cleared, waiting for return.");
  } else {
    bottleMissing = false;

    if (!baselineValid) {
      baselineWeight = stableW;
      baselineValid = true;
      hw.saveBaseline(baselineWeight);
      lastDrinkCheckMs = now;
      LOG2("Startup: baseline initialized from current weight: ", baselineWeight);
    } else {
      float drift = fabsf(stableW - baselineWeight);
      if (drift >= BOOT_REBASE_DELTA) {
        baselineWeight = stableW;
        hw.saveBaseline(baselineWeight);
        LOG2("Startup: stale baseline rebased to: ", baselineWeight);
      } else {
        LOG2("Startup: baseline kept from NVM: ", baselineWeight);
      }
    }

    // Immediate first daytime check after boot if baseline already existed.
    if (hadBaseline) {
      forceImmediateEvaluation = true;
      LOG("Startup: first interval check forced for daytime.");
    }
  }

  startupInitDone = true;
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
      waitingPresenceSinceMs = 0;
      presenceRetryNotBeforeMs = 0;

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

  // --- Before time sync timeout: rainbow and nothing else ---
  if (!timeSync.isSynced() && !timeSync.isTimedOut()) {
    hw.animateRainbow(30);
    return;  // only early return in the whole loop
  }

  // --- One-time: stop rainbow after sync or timeout fallback ---
  static bool rainbowDone = false;
  if (!rainbowDone) {
    rainbowDone = true;
    hw.setRgb(0, 0, 0);
    hw.setLed(false);
    hw.setBuzzer(false);
    if (timeSync.isSynced()) {
      LOG("Time synced. Rainbow off.");
    } else {
      LOG("Time sync timeout fallback active. Continuing hydration logic.");
    }
  }

  unsigned long now = millis();
  runStartupInit(now);
  now = millis();  // startup init uses short delays; refresh now afterwards.

  // --- Presence reply timeout guard ---
  handlePresenceTimeout(now);

  // --- Compute time state ---
  bool hasTime = timeSync.isSynced();
  bool isSleep = false;
  if (hasTime) {
    int hour = timeSync.getHour();
    isSleep = (hour >= SLEEP_START_HOUR || hour < SLEEP_END_HOUR);
  } else {
    // Timeout fallback mode: run daytime logic until a valid sync arrives.
    isSleep = false;
    if (!fallbackTimeLogged) {
      fallbackTimeLogged = true;
      LOG("No time sync yet -> using daytime fallback and skipping day reset.");
    }
  }

  // --- Day tracking (reset daily total at midnight) ---
  if (hasTime) {
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
  // SECTION 2: Bottle missing / present transitions (hysteresis + confirm)
  // ====================================================
  float currentW = hw.getWeight();
  updateBottlePresence(now, currentW);

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
      evaluateWeight(stableW, false);
    }
    // While stabilizing, skip interval check but DO run visuals below.
  }

  // ====================================================
  // SECTION 4: Interval drinking logic (daytime, bottle present, not stabilizing)
  // ====================================================
  else if (!isSleep &&
           !bottleMissing &&
           !drinkAlertActive &&
           !waitingPresence &&
           timeReached(now, presenceRetryNotBeforeMs)) {
    float wNow = hw.getWeight();
    bool retryDue = (presenceRetryNotBeforeMs != 0) && timeReached(now, presenceRetryNotBeforeMs);
    if (retryDue) {
      presenceRetryNotBeforeMs = 0;
      LOG("Presence retry window reached. Re-evaluating.");
    }

    if (!baselineValid) {
      // Baseline missing at runtime (e.g. booted without bottle then placed).
      evaluateWeight(wNow);
    } else if (forceImmediateEvaluation ||
               retryDue ||
               now - lastDrinkCheckMs >= DRINK_CHECK_INTERVAL_MS) {
      forceImmediateEvaluation = false;
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
    if (since >= DRINK_ALERT_BUZZER_DELAY_MS) {
      hw.setBuzzer(!drinkBlinkOn);  // buzzer ON when lights OFF (split peak current)
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
