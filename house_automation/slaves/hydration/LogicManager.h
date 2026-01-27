#ifndef LOGIC_MANAGER_H
#define LOGIC_MANAGER_H

#include "Hardware.h"
#include "SlaveComms.h"
#include "SlaveConfig.h"

enum State {
  STATE_MONITORING,        // Counting down interval
  STATE_WAIT_FOR_PRESENCE, // Interval done, checking if user is home (New)
  STATE_REMINDER_PRE,      // LED Blink
  STATE_REMINDER_ACTIVE,   // LED + Buzzer
  STATE_REMOVED_DRINKING,  // Bottle off
  STATE_MISSING_ALERT,     // Bottle off too long
  STATE_STABILIZING        // Bottle back, settling
};

class LogicManager {
private:
  HydrationHW *hw;
  SlaveComms *comms;

  State currentState = STATE_MONITORING;
  unsigned long stateStartTime = 0;
  unsigned long lastIntervalReset = 0;
  unsigned long lastBlinkTime = 0;
  unsigned long lastAwayCheck = 0;

  bool isBlinkOn = false;
  float lastSavedWeight = 0.0;
  float dailyTotal = 0.0;
  int currentDay = 0;
  bool isSleeping = false;

public:
  void begin(HydrationHW *hardware, SlaveComms *communicator) {
    this->hw = hardware;
    this->comms = communicator;

    // Load persisted state
    hw->loadHydrationState(&lastSavedWeight, &dailyTotal, &currentDay);
    lastIntervalReset = millis(); // Start timer

    // Initial Color
    hw->setRgb(COLOR_IDLE);
    Serial.println("Logic: Started. State loaded.");
  }

  void update() {
    float currentWeight = hw->getWeight();
    unsigned long now = millis();

    // --- State Machine ---
    switch (currentState) {

    // 1. MONITORING: Waiting for interval
    case STATE_MONITORING:
      // Global Missing Check (Immediate transition)
      if (currentWeight < THRESHOLD_WEIGHT) {
        Serial.println("Logic: Bottle Lifted (Drinking/Refilling)...");
        enterState(STATE_REMOVED_DRINKING);
        return;
      }

      // Interval Check
      if (now - lastIntervalReset > CHECK_INTERVAL_MS) {
        if (isSleeping) {
          return;
        }

        // Compare Weight
        float delta = lastSavedWeight - currentWeight;

        if (delta >= DRINK_MIN_ML) {
          // User drank proactively! Reset timer.
          Serial.print("Logic: Proactive Drink Detected (");
          Serial.print(delta);
          Serial.println("ml). Resetting Timer.");

          processDrink(delta);
          lastIntervalReset = now;
        } else {
          // No drink -> Check Presence before Reminding
          Serial.print("Logic: Interval Expired (");
          Serial.print(now - lastIntervalReset);
          Serial.println("ms > limit). Checking Presence...");

          comms->send(CMD_REQUEST_PRESENCE, 0);
          enterState(STATE_WAIT_FOR_PRESENCE);
        }
      }
      break;

    // 2. WAIT FOR PRESENCE (New State)
    case STATE_WAIT_FOR_PRESENCE:
      if (currentWeight < THRESHOLD_WEIGHT) {
        enterState(STATE_REMOVED_DRINKING);
        return;
      }

      // Timeout if Pi doesn't reply (10s) -> Default to Away (Snooze)
      if (now - stateStartTime > 10000) {
        Serial.println("Logic: Presence Timeout. Defaulting to AWAY (Snooze).");
        enterState(STATE_MONITORING);
        lastIntervalReset = millis(); // Snooze timer
      }
      break;

    // 3. REMINDER PRE: LED Only
    case STATE_REMINDER_PRE:
      if (currentWeight < THRESHOLD_WEIGHT) {
        Serial.println("Logic: Bottle Lifted! Reminder Silenced.");
        enterState(STATE_REMOVED_DRINKING);
        return;
      }

      handleBlink(now, COLOR_ALERT);

      if (now - stateStartTime > LED_ALERT_DURATION) {
        Serial.println("Logic: Pre-Alert Timeout -> Escalating to Buzzer.");
        enterState(STATE_REMINDER_ACTIVE);
      }
      break;

    // 4. REMINDER ACTIVE: LED + Buzzer + Away Check
    case STATE_REMINDER_ACTIVE:
      if (currentWeight < THRESHOLD_WEIGHT) {
        Serial.println("Logic: Bottle Lifted! Reminder Silenced.");
        enterState(STATE_REMOVED_DRINKING);
        return;
      }

      handleBlink(now, COLOR_ALERT);
      // Buzz with blink
      if (now - lastBlinkTime < 250)
        hw->setBuzzer(true);
      else
        hw->setBuzzer(false);

      // Away Check (Every 1 min)
      if (now - lastAwayCheck > AWAY_CHECK_INTERVAL_MS) {
        lastAwayCheck = now;
        Serial.println("Logic: Checking Presence (Smart Silence)...");
        comms->send(CMD_REQUEST_PRESENCE, 0);
      }
      break;

    // 5. REMOVED: Drinking or Missing?
    case STATE_REMOVED_DRINKING:
      hw->stopAll();

      // If back -> Stabilize
      if (currentWeight >= THRESHOLD_WEIGHT) {
        Serial.println("Logic: Bottle Returned. Waiting to Stabilize...");
        enterState(STATE_STABILIZING);
        return;
      }

      // If gone too long -> Missing Alert
      if (now - stateStartTime > MISSING_TIMEOUT_MS) {
        Serial.println("Logic: Bottle Missing for too long (>10s) -> "
                       "triggering MISSING Alert.");
        enterState(STATE_MISSING_ALERT);
        comms->send(CMD_ALERT_MISSING, 0);
      }
      break;

    // 6. MISSING ALERT: The "No Bottle" Alarm
    case STATE_MISSING_ALERT:
      if (currentWeight >= THRESHOLD_WEIGHT) {
        Serial.println("Logic: Missing Bottle Found!");
        comms->send(CMD_ALERT_REPLACED, 0);
        enterState(STATE_STABILIZING);
        return;
      }

      // Custom Alert Pattern for Missing (Red Blink)
      if (now - lastBlinkTime > 500) {
        lastBlinkTime = now;
        isBlinkOn = !isBlinkOn;
        hw->setLed(isBlinkOn);
        hw->setRgb(isBlinkOn ? COLOR_ALERT : 0);
      }

      // Buzz after delay
      if (now - stateStartTime > BUZZER_START_DELAY_MS) {
        hw->setBuzzer(isBlinkOn);
      }
      break;

    // 7. STABILIZING: Weighing
    case STATE_STABILIZING:
      hw->stopAll();

      if (now - stateStartTime > STABILIZATION_MS) {
        float finalWeight = hw->getWeight();
        Serial.print("Logic: Stabilized at ");
        Serial.print(finalWeight);
        Serial.println("g. Evaluating Result...");

        evaluateWeightChange(finalWeight);
        enterState(STATE_MONITORING);
        // lastIntervalReset updated inside evaluateWeightChange ONLY if
        // successful drink
      }
      break;
    }
  }

  // --- Helpers ---
  void enterState(State newState) {
    State oldState = currentState;
    currentState = newState;
    stateStartTime = millis();
    hw->stopAll();

    // Set Status Color based on Mode
    if (currentState == STATE_MONITORING) {
      hw->setRgb(isSleeping ? COLOR_SLEEP : COLOR_IDLE);
    }

    // Check if we are stopping an active alert
    if (oldState == STATE_REMINDER_PRE || oldState == STATE_REMINDER_ACTIVE) {
      if (currentState != STATE_REMINDER_PRE &&
          currentState != STATE_REMINDER_ACTIVE) {
        Serial.println("Logic: Alert Stopped -> Sending Notification");
        comms->send(CMD_ALERT_STOPPED, 0);
      }
    }
  }

  void handleBlink(unsigned long now, int color) {
    if (now - lastBlinkTime > 500) {
      lastBlinkTime = now;
      isBlinkOn = !isBlinkOn;
      hw->setLed(isBlinkOn);
      if (color > 0)
        hw->setRgb(isBlinkOn ? color : 0);
    }
  }

  void evaluateWeightChange(float currentWeight) {
    float diff = lastSavedWeight - currentWeight;

    // Drank?
    if (diff >= DRINK_MIN_ML) {
      Serial.print("RESULT: User Drank ");
      Serial.print(diff);
      Serial.println("ml. (Good job!)");

      processDrink(diff);
      hw->setRgb(COLOR_OK); // Green confirm
      delay(2000);
      hw->setRgb(0);
      lastSavedWeight = currentWeight;

      lastIntervalReset = millis(); // Reset Timer
    }
    // Refilled?
    else if (diff <= -REFILL_MIN_ML) {
      Serial.print("RESULT: Bottle Refilled (+");
      Serial.print(-diff);
      Serial.println("ml).");

      hw->setRgb(COLOR_REFILL); // Blue confirm
      delay(2000);
      hw->setRgb(0);
      lastSavedWeight = currentWeight;

      lastIntervalReset = millis(); // Reset Timer
    }
    // Small change?
    else {
      Serial.println("RESULT: No significant change (Preserving Baseline).");
      // DO NOT UPDATE lastSavedWeight.
      // DO NOT UPDATE lastIntervalReset.
      // This prevents "resetting" the counter on small deviations or quick
      // replace. Small sips will accumulate until they cross DRINK_MIN_ML.
    }

    // Save state
    hw->saveHydrationState(lastSavedWeight, dailyTotal, currentDay);
  }

  void processDrink(float amount) {
    dailyTotal += amount;
    // Send to Pi
    comms->sendFloat(CMD_DRINK_DETECTED, amount);
    comms->sendFloat(CMD_DAILY_TOTAL, dailyTotal);
  }

  // Called from hydration.ino when presence report arrives
  void handlePresence(bool isHome) {
    Serial.print("Logic: Presence Update -> ");
    Serial.println(isHome ? "HOME" : "AWAY");

    // If User is AWAY, silence everything
    if (!isHome) {
      if (currentState == STATE_WAIT_FOR_PRESENCE ||
          currentState == STATE_REMINDER_PRE ||
          currentState == STATE_REMINDER_ACTIVE) {

        Serial.println("Logic: User Away. Snoozing/Silencing Reminder.");
        enterState(STATE_MONITORING);
        lastIntervalReset = millis(); // Reset timer (Snooze)
      }
      return;
    }

    // If User is HOME
    if (currentState == STATE_WAIT_FOR_PRESENCE) {
      Serial.println("Logic: User Home. Starting Reminder.");
      enterState(STATE_REMINDER_PRE);
      comms->send(CMD_ALERT_REMINDER, 0); // Notify Pi
    }
  }

  // Called periodically to check for new day
  void checkDay(int newDay) {
    if (currentDay != newDay) {
      Serial.print("Logic: New Day Detected (");
      Serial.print(currentDay);
      Serial.print(" -> ");
      Serial.print(newDay);
      Serial.println("). Resetting Daily Total.");

      currentDay = newDay;
      dailyTotal = 0.0;

      // Save Reset State
      hw->saveHydrationState(lastSavedWeight, dailyTotal, currentDay);

      // Notify Pi of reset
      comms->sendFloat(CMD_DAILY_TOTAL, dailyTotal);
    }
  }

  void setSleep(bool sleeping) {
    bool changed = (isSleeping != sleeping);
    isSleeping = sleeping;

    if (changed) {
      Serial.print("Logic: Sleep Mode ");
      Serial.println(isSleeping ? "ACTIVATED (Zzz...)"
                                : "DEACTIVATED (Good Morning!)");

      // Visual Update if Monitoring
      if (currentState == STATE_MONITORING) {
        hw->setRgb(isSleeping ? COLOR_SLEEP : COLOR_IDLE);
      }

      // Auto-Silence if alerting
      if (isSleeping && (currentState == STATE_REMINDER_PRE ||
                         currentState == STATE_REMINDER_ACTIVE)) {
        Serial.println("Logic: Sleep Logic Silencing Active Alert.");
        enterState(STATE_MONITORING);
      }
    }
  }
};
#endif
