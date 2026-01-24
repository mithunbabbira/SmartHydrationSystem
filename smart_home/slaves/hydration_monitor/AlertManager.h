#ifndef ALERT_MANAGER_H
#define ALERT_MANAGER_H

#include "config.h"
#include <Arduino.h>

class AlertManager {
public:
  uint8_t currentLevel = 0;

  void begin() {
    pinMode(PIN_RED, OUTPUT);
    pinMode(PIN_GREEN, OUTPUT);
    pinMode(PIN_BLUE, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);

// Legacy Support
#ifdef PIN_ALERT_LED
    pinMode(PIN_ALERT_LED, OUTPUT);
#endif

    reset();
  }

  void setLevel(uint8_t level) {
    if (currentLevel != level) {
      currentLevel = level;
      Serial.printf("! Alert Level Set to: %d\n", currentLevel);
      reset(); // Clear state when level changes
    }
  }

  void update() {
    if (currentLevel == 0) {
      reset();
      return;
    }

    unsigned long now = millis();
    int interval =
        (currentLevel == 1) ? ALERT_BLINK_WARNING_MS : ALERT_BLINK_CRITICAL_MS;

    if (now - lastBlink > interval) {
      lastBlink = now;
      state = !state;
      updateHardware();
    }
  }

private:
  unsigned long lastBlink = 0;
  bool state = false;

  void reset() {
    digitalWrite(PIN_RED, LOW);
    digitalWrite(PIN_GREEN, LOW);
    digitalWrite(PIN_BLUE, LOW);
    digitalWrite(PIN_BUZZER, LOW);
#ifdef PIN_ALERT_LED
    digitalWrite(PIN_ALERT_LED, LOW);
#endif
    state = false;
  }

  void updateHardware() {
// Redundant Alerting: Toggle both Legacy and RGB pins

// 1. Legacy LED (Pin 25)
#ifdef PIN_ALERT_LED
    digitalWrite(PIN_ALERT_LED, state ? HIGH : LOW);
#endif

    // 2. RGB LED
    if (currentLevel == 1) {
      // Warning: Blue
      digitalWrite(PIN_BLUE, state ? HIGH : LOW);
      digitalWrite(PIN_RED, LOW);
    } else {
      // Critical: Red
      digitalWrite(PIN_RED, state ? HIGH : LOW);
      digitalWrite(PIN_BLUE, LOW);
    }

    // 3. Buzzer (Level 2 Only, Beep on Active Phase)
    if (state && currentLevel >= 2) {
      digitalWrite(PIN_BUZZER, HIGH);
      delay(ALERT_BEEP_DURATION_MS); // Simple blocking beep (50ms) is okay
      digitalWrite(PIN_BUZZER, LOW);
    }
  }
};

#endif
