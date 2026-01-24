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

    _prefs.begin("logic", false);
    _intervalStartWeight = _prefs.getFloat("startW", 0);
    _lastCheckEpoch = _prefs.getULong("lastCheck", 0);

    _lastSync = millis();
    _comms->sendQuery(1);
    _comms->sendQuery(2);
  }

  // Helper for Timestamping Logs (HH:MM:SS)
  String getFormattedTime() {
    if (_serverEpoch == 0)
      return "[No Time]";
    uint32_t local =
        _serverEpoch + ((millis() - _lastEpochSync) / 1000) + 19800; // IST
    uint8_t h = (local % 86400) / 3600;
    uint8_t m = (local % 3600) / 60;
    uint8_t s = (local % 60);
    char buf[16];
    sprintf(buf, "[%02d:%02d:%02d]", h, m, s);
    return String(buf);
  }

  void update(float currentWeight, bool isMissing) {
    unsigned long now = millis();

    // 1. Sync Time / Presence
    if (now - _lastSync > 60000) {
      _lastSync = now;
      _comms->sendQuery(1);
      _comms->sendQuery(2);
    }

    // Update Internal Clock
    if (_serverEpoch > 0) {
      _currentEpoch = _serverEpoch + ((now - _lastEpochSync) / 1000);
    }

    // 2. State Gating
    if (isMissing) {
      _lastWeight = 0;         // Invalidate last weight
      _stabilityStartTime = 0; // Reset stability
      return;
    }

    // 3. Weight Stabilization (Debounce)
    if (abs(currentWeight - _lastRawWeight) > 5.0) { // 5g noise threshold
      _stabilityStartTime = now;
      _lastRawWeight = currentWeight;
      // Serial.println("Unstable...");
      return; // Unstable
    }

    if (now - _stabilityStartTime < 2000) { // 2 Seconds Wait
      return;                               // Waiting for stability
    }

    // Weight is STABLE. Use it.
    float stableWeight = currentWeight;

    // 4. Sleep Check (With Logging)
    if (isSleeping()) {
      if (now - _lastSleepLog > 60000) { // Log once a minute
        _lastSleepLog = now;
        Serial.print(getFormattedTime());
        Serial.println(" Status: Sleeping (Logic Paused)");
      }
      if (_alerts->currentLevel != 0)
        _alerts->setLevel(0);
      return;
    }

    // 5. Presence Check (With Logging)
    // Only skip checks if CONFIRMED Away (default True)
    // If we haven't synced presence yet, assume Home.
    if (_serverEpoch > 0 && !_isHome) {
      if (now - _lastPresenceLog > 60000) {
        _lastPresenceLog = now;
        Serial.print(getFormattedTime());
        Serial.println(" Status: Away (Logic Paused)");
      }
      if (_alerts->currentLevel != 0)
        _alerts->setLevel(0);
      return;
    }

    // 6. Hydration Logic
    if (_lastWeight == 0) {
      _lastWeight = stableWeight;
      if (_intervalStartWeight == 0)
        _intervalStartWeight = stableWeight;
      return;
    }

    float delta = stableWeight - _lastWeight;

    // Refill Detection (Significant Increase)
    if (delta >= REFILL_THRESHOLD) {
      Serial.print(getFormattedTime());
      Serial.println(" Refill Detected -> Resetting Interval Baseline.");
      _intervalStartWeight = stableWeight;
      _lastWeight = stableWeight;
      _alerts->setLevel(0);
      _prefs.putFloat("startW", _intervalStartWeight);
    }
    // Drink Detection (Significant Decrease)
    else if (delta <= -DRINK_THRESHOLD_MIN) {
      float amount = abs(delta);
      Serial.print(getFormattedTime());
      Serial.print(" Drink Detected: ");
      Serial.println(amount);
      _lastWeight = stableWeight;
      _alerts->setLevel(0);
      _lastDrinkTime = now;
    }

    // 7. Interval Check (Only check if stable)
    // We use Epoch for robust interval timing across reboots
    if (_serverEpoch > 0) {
      if (_lastCheckEpoch == 0)
        _lastCheckEpoch = _currentEpoch;

      if (_currentEpoch - _lastCheckEpoch > (CHECK_INTERVAL_MS / 1000)) {
        Serial.print(getFormattedTime());
        Serial.println(" Performing Hydration Check...");

        if (_intervalStartWeight == 0)
          _intervalStartWeight = stableWeight;
        float consumption = _intervalStartWeight - stableWeight;

        if (consumption >= DRINK_THRESHOLD_MIN) {
          Serial.print(getFormattedTime());
          Serial.printf(" ✓ Goal Met (Consumed %.1fg)\n", consumption);
          _alerts->setLevel(3); // Green Success
        } else {
          Serial.print(getFormattedTime());
          Serial.printf(" ❌ Goal Not Met (Only %.1fg)\n", consumption);
          _alerts->setLevel(1);
        }

        // Reset
        _intervalStartWeight = stableWeight;
        _lastCheckEpoch = _currentEpoch;
        _prefs.putFloat("startW", _intervalStartWeight);
        _prefs.putULong("lastCheck", _lastCheckEpoch);
      }
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

  float _lastRawWeight = 0;
  float _lastWeight = 0;
  float _intervalStartWeight = 0;

  unsigned long _lastSync = 0;
  unsigned long _stabilityStartTime = 0;
  unsigned long _lastDrinkTime = 0;

  unsigned long _lastSleepLog = 0;
  unsigned long _lastPresenceLog = 0;

  uint32_t _serverEpoch = 0;
  unsigned long _lastEpochSync = 0;
  uint32_t _currentEpoch = 0;
  unsigned long _lastCheckEpoch = 0;
  bool _isHome = true;

  bool isSleeping() {
    if (_serverEpoch == 0)
      return false;
    uint32_t local =
        _serverEpoch + ((millis() - _lastEpochSync) / 1000) + 19800; // IST
    uint8_t hour = (local % 86400) / 3600;
    if (SLEEP_START_HOUR > SLEEP_END_HOUR)
      return (hour >= SLEEP_START_HOUR || hour < SLEEP_END_HOUR);
    else
      return (hour >= SLEEP_START_HOUR && hour < SLEEP_END_HOUR);
  }
};

#endif
