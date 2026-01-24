#ifndef LOGIC_MANAGER_H
#define LOGIC_MANAGER_H

#include "AlertManager.h"
#include "CommsManager.h"
#include "config.h"

class LogicManager {
public:
  void begin(CommsManager *comms, AlertManager *alerts) {
    _comms = comms;
    _alerts = alerts;
    _lastCheck = millis();
  }

  void update(float currentWeight, bool isMissing) {
    unsigned long now = millis();

    // 1. Sync Time / Presence periodically (e.g. every minute)
    if (now - _lastSync > 60000) {
      _lastSync = now;
      _comms->sendQuery(1); // 1 = Time
      _comms->sendQuery(2); // 2 = Presence
    }

    // Update Internal Clock (approximation)
    if (_serverEpoch > 0) {
      _currentEpoch = _serverEpoch + ((now - _lastEpochSync) / 1000);
    }

    // 2. Sleep Check
    if (isSleeping()) {
      if (_alerts->currentLevel != 0)
        _alerts->setLevel(0);
      return;
    }

    // 3. Presence Check
    if (!_isHome) {
      if (_alerts->currentLevel != 0)
        _alerts->setLevel(0);
      return;
    }

    // 4. Missing Check (Handled by Immediate Logic in Loop)
    if (isMissing) {
      if (now - _missingStart > BOTTLE_MISSING_TIMEOUT_MS) {
        // Escalated Alert?
      } else {
        _missingStart = now;
      }
      return;
    }

    // 5. Hydration Logic (Drink Detection)
    if (_lastWeight == 0) {
      _lastWeight = currentWeight;
      return;
    }

    float delta = currentWeight - _lastWeight;

    // Drink
    if (delta <= -DRINK_THRESHOLD_MIN) {
      float amount = abs(delta);
      _todayConsumption += amount;
      // Serial.printf("Drink: %.1fml (Total: %.1fml)\n", amount,
      // _todayConsumption); // Safe in Loop
      Serial.print("Drink Detected: ");
      Serial.println(amount);
      _lastWeight = currentWeight;
      _alerts->setLevel(0);
      _lastDrinkTime = now;
    }
    // Refill
    else if (delta >= REFILL_THRESHOLD) {
      Serial.println("Refill Detected.");
      _lastWeight = currentWeight;
      _alerts->setLevel(0);
    }

    // Check Timer
    if (now - _lastDrinkTime > CHECK_INTERVAL_MS) {
      if (_todayConsumption < DAILY_GOAL_ML) {
        // Trigger Reminder
        _alerts->setLevel(1);
      }
    }
  }

  void handleTimeResponse(uint32_t epoch) {
    _serverEpoch = epoch;
    _lastEpochSync = millis();
    // Serial.printf("Time Synced: %u\n", epoch);
  }

  void handlePresenceResponse(bool isHome) {
    _isHome = isHome;
    // Serial.printf("Presence Synced: %s\n", isHome ? "HOME" : "AWAY");
  }

private:
  CommsManager *_comms;
  AlertManager *_alerts;

  float _lastWeight = 0;
  float _todayConsumption = 0;

  unsigned long _lastCheck = 0;
  unsigned long _lastDrinkTime = 0;
  unsigned long _lastSync = 0;
  unsigned long _missingStart = 0;

  uint32_t _serverEpoch = 0;
  unsigned long _lastEpochSync = 0;
  uint32_t _currentEpoch = 0;

  bool _isHome = true; // Default true

  bool isSleeping() {
    if (_serverEpoch == 0)
      return false; // No time yet

    uint32_t local =
        _serverEpoch + ((millis() - _lastEpochSync) / 1000) + 19800; // IST
    uint8_t hour = (local % 86400) / 3600;

    if (SLEEP_START_HOUR > SLEEP_END_HOUR) {
      return (hour >= SLEEP_START_HOUR || hour < SLEEP_END_HOUR);
    } else {
      return (hour >= SLEEP_START_HOUR && hour < SLEEP_END_HOUR);
    }
  }
};

#endif
