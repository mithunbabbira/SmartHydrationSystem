#ifndef LOGIC_MANAGER_H
#define LOGIC_MANAGER_H

#include "AlertManager.h"
#include "CommsManager.h"
#include "config.h"
#include <Arduino.h>
#include <Preferences.h>

class LogicManager {
public:
  void begin(CommsManager *comms, AlertManager *alerts) {
    _comms = comms;
    _alerts = alerts;

    // NVM Storage
    _prefs.begin("logic", false);
    _intervalStartWeight = _prefs.getFloat("startW", 0);
    // We track last check by Time (millis is reset on boot, so checks might
    // reset on boot). User said "you decide when best to save". Saving
    // `lastCheckEpoch` is better if we have synced time.
    _lastCheckEpoch = _prefs.getULong("lastCheck", 0);

    _lastSync = millis();
    // Force Query on Boot
    _comms->sendQuery(1);
    _comms->sendQuery(2);
  }

  void update(float currentWeight, bool isMissing) {
    unsigned long now = millis();

    // 1. Sync Time / Presence periodically (e.g. every minute)
    if (now - _lastSync > 60000) {
      _lastSync = now;
      _comms->sendQuery(1);
      _comms->sendQuery(2);
    }

    // Update Internal Clock
    if (_serverEpoch > 0) {
      _currentEpoch = _serverEpoch + ((now - _lastEpochSync) / 1000);
    }

    // 2. Logic Gates (Sleep / Away) - Only if we have context
    if (_serverEpoch > 0 && isSleeping()) {
      if (_alerts->currentLevel != 0)
        _alerts->setLevel(0);
      return;
    }
    if (!_isHome) { // Default true, so robust if no sync
      if (_alerts->currentLevel != 0)
        _alerts->setLevel(0);
      return;
    }

    // 3. Hydration Interval Check
    // Logic: Has weight reduced by DRINK_THRESHOLD_MIN since Interval Start?
    // We use Epoch for robust interval timing across reboots
    if (_serverEpoch > 0) {
      if (_lastCheckEpoch == 0)
        _lastCheckEpoch = _currentEpoch; // Init

      if (_currentEpoch - _lastCheckEpoch > (CHECK_INTERVAL_MS / 1000)) {

        // Interval Reached. Perform Check.
        Serial.println("Performing Hydration Check...");

        if (_intervalStartWeight == 0)
          _intervalStartWeight = currentWeight; // First run correction

        float consumption = _intervalStartWeight - currentWeight;
        // If consumption is NEGATIVE (Refilled), we treat as success?
        // Or just reset logic.

        bool drunkEnough = (consumption >= DRINK_THRESHOLD_MIN);

        if (drunkEnough) {
          Serial.printf("✓ Goal Met (Consumed %.1fg)\n", consumption);
          _alerts->setLevel(0);
        } else {
          Serial.printf("❌ Goal Not Met (Only %.1fg)\n", consumption);
          // Trigger Alert 1 (Warning)
          _alerts->setLevel(1);
        }

        // Reset for next interval
        _intervalStartWeight = currentWeight;
        _lastCheckEpoch = _currentEpoch;

        // Save State
        _prefs.putFloat("startW", _intervalStartWeight);
        _prefs.putULong("lastCheck", _lastCheckEpoch);
      }
    }

    // 4. Real-time Drink/Refill Detection (Updates baseline)
    if (_lastWeight == 0) {
      _lastWeight = currentWeight;
      if (_intervalStartWeight == 0)
        _intervalStartWeight = currentWeight;
      return;
    }

    float delta = currentWeight - _lastWeight;

    // Refill Event
    if (delta >= REFILL_THRESHOLD) {
      Serial.println("Refill Detected -> Resetting Interval Baseline.");
      _intervalStartWeight = currentWeight; // Reset baseline so we don't demand
                                            // drinking from empty
      _lastWeight = currentWeight;
      _alerts->setLevel(0);

      _prefs.putFloat("startW", _intervalStartWeight);
    }
    // Drink Event (Just for logging or correcting baseline if we want "interval
    // to start from now"?) User requirement: "reduced atleast ... from previous
    // weight recorded" (Start of interval). If I drink, currentWeight drops.
    // `start - current` increases. So drinking is GOOD. We don't need to reset
    // baseline on drink.
    else if (delta <= -DRINK_THRESHOLD_MIN) {
      float amount = abs(delta);
      Serial.printf("Drink Detected: %.1f\n", amount);
      _lastWeight = currentWeight;
      _alerts->setLevel(0); // Clear alert if they drink

      // Optional: If they drink enough, maybe push next check out?
      // "you decide when best".
      // Let's reset the timer if they drink significantly?
      // That feels smarter. If I drink now, don't bug me in 5 mins because
      // interval ended. Yes. Reset Timer on Drink? "check if weight has reduced
      // ... from previous". If I drink, weight DID reduce. So if the check
      // happens, it passes. Leaving timer running is fine. It will pass.
    }
  }

  void handleTimeResponse(uint32_t epoch) {
    _serverEpoch = epoch;
    _lastEpochSync = millis();
  }

  void handlePresenceResponse(bool isHome) { _isHome = isHome; }

private:
  CommsManager *_comms;
  AlertManager *_alerts;
  Preferences _prefs;

  float _lastWeight = 0;
  float _intervalStartWeight = 0; // Baseline at start of interval

  unsigned long _lastSync = 0;
  unsigned long _missingStart = 0;

  uint32_t _serverEpoch = 0;
  unsigned long _lastEpochSync = 0;
  uint32_t _currentEpoch = 0;

  // Persisted Loop State
  unsigned long _lastCheckEpoch = 0;

  bool _isHome = true;

  bool isSleeping() {
    if (_serverEpoch == 0)
      return false;

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
