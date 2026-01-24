#ifndef SCALE_MANAGER_H
#define SCALE_MANAGER_H

#include "config.h"
#include <HX711.h>
#include <Preferences.h>

class ScaleManager {
public:
  void begin() {
    Serial.println("ScaleManager: Initializing...");
    scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
    scale.set_scale(CALIBRATION_FACTOR);

    // Non-blocking wait (2 seconds max) to prevent hang
    if (scale.wait_ready_timeout(2000)) {
      loadTare();
    } else {
      Serial.println("⚠ Scale NOT ready! Check wiring. Continuing...");
      ready = false;
    }
  }

  float readWeight() {
    if (!ready && scale.is_ready()) {
      ready = true; // Recovered
      loadTare();
    }
    if (ready) {
      return scale.get_units(5); // Average 5 readings
    }
    return 0.0;
  }

  void tare() {
    if (!ready)
      return;
    Serial.println("ScaleManager: Taring...");
    scale.tare();
    saveTare(scale.get_offset());
  }

private:
  HX711 scale;
  Preferences prefs;
  bool ready = false;

  void loadTare() {
    prefs.begin("hydration", false);
    long savedOffset = prefs.getLong("tareOffset", 0);

    if (savedOffset != 0) {
      scale.set_offset(savedOffset);
      Serial.printf("✓ Loaded saved tare offset: %ld\n", savedOffset);
    } else {
      Serial.println("⚠ No saved tare. Taring now...");
      scale.tare();
      saveTare(scale.get_offset());
    }
    prefs.end();
    ready = true;
  }

  void saveTare(long offset) {
    prefs.begin("hydration", false);
    prefs.putLong("tareOffset", offset);
    prefs.end();
    Serial.printf("✓ Saved new tare offset: %ld\n", offset);
  }
};

#endif
