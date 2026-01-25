#ifndef LOGIC_MANAGER_H
#define LOGIC_MANAGER_H

#include "Hardware.h"
#include "SlaveComms.h"

// --- Configuration ---
#define MISSING_TIMEOUT_MS 10000   // 10 seconds before alert
#define BUZZER_START_DELAY_MS 5000 // Wait 5s during alert before buzzing
#define STABILIZATION_MS 2000      // Wait 2s after bottle replaced
#define THRESHOLD_WEIGHT 80.0      // Min weight to consider bottle detected

enum State {
  STATE_IDLE,            // Bottle Present
  STATE_REMOVED_PENDING, // Weight low, timer started
  STATE_MISSING_ALERT,   // Timer expired, alerting
  STATE_STABILIZING      // Bottle back, waiting for value to settle
};

class LogicManager {
private:
  HydrationHW *hw;
  SlaveComms *comms;

  State currentState = STATE_IDLE;
  unsigned long stateStartTime = 0;
  unsigned long lastBlinkTime = 0;
  bool isBlinkOn = false;

public:
  void begin(HydrationHW *hardware, SlaveComms *communicator) {
    this->hw = hardware;
    this->comms = communicator;
  }

  void update() {
    float currentWeight = hw->getWeight(); // Get current weight
    unsigned long now = millis();

    switch (currentState) {
    // --- 1. IDLE: Normal Monitoring ---
    case STATE_IDLE:
      if (currentWeight < THRESHOLD_WEIGHT) {
        // Weight dropped -> Start Missing Timer
        currentState = STATE_REMOVED_PENDING;
        stateStartTime = now;
        Serial.println("Logic: Bottle Removed... Waiting for timeout.");
      }
      break;

    // --- 2. PENDING: Wait 10s ---
    case STATE_REMOVED_PENDING:
      // If bottle returned immediately, cancel
      if (currentWeight >= THRESHOLD_WEIGHT) {
        currentState = STATE_IDLE;
        Serial.println("Logic: Bottle Returned (False Alarm).");
        return;
      }

      // If timeout reached -> TRIGGER ALERT
      if (now - stateStartTime > MISSING_TIMEOUT_MS) {
        currentState = STATE_MISSING_ALERT;
        stateStartTime = now; // Reset timer for alert sequencing

        Serial.println("Logic: MISSING DETECTED! Starting Alert.");
        comms->send(CMD_ALERT_MISSING, 0);

        // Initial Alert State
        hw->setRgb(0); // Clear RGB
        // (LED blinking handled in loop below)
      }
      break;

    // --- 3. ALERT: LED + Buzzer ---
    case STATE_MISSING_ALERT:
      // Recovery Check
      if (currentWeight >= THRESHOLD_WEIGHT) {
        stopAlert();
        currentState = STATE_STABILIZING;
        stateStartTime = now;

        Serial.println("Logic: Bottle Replaced! Stabilizing...");
        comms->send(CMD_ALERT_REPLACED, 0);
        return;
      }

      // Action: Blink White LED (Every 500ms)
      if (now - lastBlinkTime > 500) {
        lastBlinkTime = now;
        isBlinkOn = !isBlinkOn;
        hw->setLed(isBlinkOn);
        // Set RGB Orange when LED is ON for effect
        // 4 = White, 5 = Orange (Manual)
        // Let's use Red for Alert since 'Orange' isn't in simple switch
        hw->setRgb(isBlinkOn ? 1 : 0);
      }

      // Action: Buzz after 5 seconds delay
      if (now - stateStartTime > BUZZER_START_DELAY_MS) {
        // Beep with the LED blink
        hw->setBuzzer(isBlinkOn);
      }
      break;

    // --- 4. STABILIZING: Wait for shake to settle ---
    case STATE_STABILIZING:
      if (now - stateStartTime > STABILIZATION_MS) {
        currentState = STATE_IDLE;
        Serial.println("Logic: Stabilized. Resuming Normal.");
      }
      break;
    }
  }

  void stopAlert() {
    hw->setLed(false);
    hw->setBuzzer(false);
    hw->setRgb(0);
  }

  bool isStabilizing() { return (currentState == STATE_STABILIZING); }
};

#endif
