#ifndef LOGIC_MANAGER_H
#define LOGIC_MANAGER_H

#include "Hardware.h"
#include "SlaveComms.h"

// --- Configuration ---
#define MISSING_TIMEOUT_MS 10000     // 10 seconds before alert
#define BUZZER_START_DELAY_MS 5000   // Wait 5s during alert before buzzing
#define STABILIZATION_MS 2000        // Wait 2s after bottle replaced
#define THRESHOLD_WEIGHT 80.0        // Min weight to consider bottle detected
#define DRINK_MIN_ML 50.0            // Minimum weight drop to count as drink
#define REFILL_MIN_ML 100.0          // Minimum weight gain to count as refill
#define CHECK_INTERVAL_MS 1800000    // 30 Minutes
#define LED_ALERT_DURATION 10000     // 10 Seconds
#define AWAY_CHECK_INTERVAL_MS 60000 // Check away every 1 min during alert

enum State {
  STATE_MONITORING,       // Counting down 30min
  STATE_REMINDER_PRE,     // LED Blink (10s)
  STATE_REMINDER_ACTIVE,  // LED + Buzzer
  STATE_REMOVED_DRINKING, // Bottle off (Drinking/Refilling)
  STATE_MISSING_ALERT,    // Bottle off too long (>10s)
  STATE_STABILIZING       // Bottle back, settling
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
        enterState(STATE_REMOVED_DRINKING);
        return;
      }

      // Interval Check (30 mins)
      if (now - lastIntervalReset > CHECK_INTERVAL_MS) {
        if (isSleeping) {
          return; // Do nothing if sleeping
        }

        // Compare Weight
        float delta = lastSavedWeight - currentWeight;

        if (delta >= DRINK_MIN_ML) {
          // User drank proactively! Reset timer.
          Serial.println("Logic: Proactive Drink Detected. Resetting Timer.");
          processDrink(delta);
          lastIntervalReset = now;
        } else {
          // No drink -> Start Reminder
          Serial.println("Logic: Interval Expired -> Starting Pre-Alert.");
          enterState(STATE_REMINDER_PRE);
        }
      }
      break;

    // 2. REMINDER PRE: LED Only (10s)
    case STATE_REMINDER_PRE:
      if (currentWeight < THRESHOLD_WEIGHT) {
        enterState(STATE_REMOVED_DRINKING);
        return;
      }

      handleBlink(now, 1); // RGB Red/Orange

      if (now - stateStartTime > LED_ALERT_DURATION) {
        Serial.println("Logic: Pre-Alert Done -> Escalating to Buzzer.");
        enterState(STATE_REMINDER_ACTIVE);
      }
      break;

    // 3. REMINDER ACTIVE: LED + Buzzer + Away Check
    case STATE_REMINDER_ACTIVE:
      if (currentWeight < THRESHOLD_WEIGHT) {
        enterState(STATE_REMOVED_DRINKING);
        return;
      }

      handleBlink(now, 1);
      // Buzz with blink
      if (now - lastBlinkTime < 250)
        hw->setBuzzer(true);
      else
        hw->setBuzzer(false);

      // Away Check (Every 1 min)
      if (now - lastAwayCheck > AWAY_CHECK_INTERVAL_MS) {
        lastAwayCheck = now;
        Serial.println("Logic: Alert Active - Checking Presence...");
        comms->send(CMD_REQUEST_PRESENCE, 0);
        // Response handled in handlePresence (below)
      }
      break;

    // 4. REMOVED: Drinking or Missing?
    case STATE_REMOVED_DRINKING:
      hw->stopAll(); // Silence alerts immediately

      // If back -> Stabilize
      if (currentWeight >= THRESHOLD_WEIGHT) {
        enterState(STATE_STABILIZING);
        return;
      }

      // If gone too long -> Missing Alert
      if (now - stateStartTime > MISSING_TIMEOUT_MS) {
        enterState(STATE_MISSING_ALERT);
        comms->send(CMD_ALERT_MISSING, 0);
      }
      break;

    // 5. MISSING ALERT: The "No Bottle" Alarm
    case STATE_MISSING_ALERT:
      if (currentWeight >= THRESHOLD_WEIGHT) {
        comms->send(CMD_ALERT_REPLACED, 0);
        enterState(STATE_STABILIZING);
        return;
      }

      // Custom Alert Pattern for Missing
      if (now - lastBlinkTime > 500) {
        lastBlinkTime = now;
        isBlinkOn = !isBlinkOn;
        hw->setLed(isBlinkOn);
        hw->setRgb(isBlinkOn ? 1 : 0); // Blink Red
      }

      // Buzz after delay
      if (now - stateStartTime > BUZZER_START_DELAY_MS) {
        hw->setBuzzer(isBlinkOn);
      }
      break;

    // 6. STABILIZING: Weighing
    case STATE_STABILIZING:
      hw->stopAll();
      if (now - stateStartTime > STABILIZATION_MS) {
        Serial.println("Logic: Stabilized.");
        evaluateWeightChange(currentWeight);
        enterState(STATE_MONITORING);
        lastIntervalReset = now; // Always reset timer after interaction
      }
      break;
    }
  }

  // --- Helpers ---
  void enterState(State newState) {
    currentState = newState;
    stateStartTime = millis();
    hw->stopAll();
    Serial.print("Logic: Entered State ");
    Serial.println(currentState);
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
      Serial.print("Logic: Drink Detected: ");
      Serial.print(diff);
      Serial.println("ml");
      processDrink(diff);
      hw->setRgb(2); // Green
      delay(2000);   // Visual Confirmation
      hw->setRgb(0);
      lastSavedWeight = currentWeight;
    }
    // Refilled?
    else if (diff <= -REFILL_MIN_ML) {
      Serial.println("Logic: Refill Detected.");
      hw->setRgb(3); // Blue
      delay(2000);
      hw->setRgb(0);
      lastSavedWeight = currentWeight;
    }
    // Small change? (Tiny sip or noise) -> Just update baseline
    else {
      Serial.println("Logic: Weight Updated (No significant drink).");
      lastSavedWeight = currentWeight;
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
    if (currentState == STATE_REMINDER_ACTIVE && !isHome) {
      Serial.println("Logic: User Away -> Silencing Reminder.");
      enterState(STATE_MONITORING);
      lastIntervalReset = millis(); // Defer reminder
    }
  }

  void setSleep(bool sleeping) {
    if (sleeping && (currentState == STATE_REMINDER_PRE ||
                     currentState == STATE_REMINDER_ACTIVE)) {
      // If alerting and sleep starts -> silence
      hw->stopAll();
      enterState(STATE_MONITORING);
    }
    isSleeping = sleeping;
  }
};

#endif
