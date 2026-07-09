#ifndef HYDRATION_V3_HARDWARE_H
#define HYDRATION_V3_HARDWARE_H

#include "Config.h"
#include "HX711.h"
#include <Arduino.h>
#include <Preferences.h>

class HydrationHW {
  HX711 scale;
  Preferences prefs;
  float lastWeight_ = 0.0f;

public:
  void begin() {
    pinMode(PIN_LED_WHITE, OUTPUT);
    digitalWrite(PIN_LED_WHITE, LOW);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    pinMode(PIN_RGB_R, OUTPUT);
    pinMode(PIN_RGB_G, OUTPUT);
    pinMode(PIN_RGB_B, OUTPUT);

    scale.begin(PIN_SCALE_DT, PIN_SCALE_SCK);
    scale.set_scale(CALIBRATION_FACTOR);
    prefs.begin("hydration", false);

    if (prefs.isKey("tare_offset")) {
      scale.set_offset(prefs.getLong("tare_offset", 0));
    } else {
      if (scale.is_ready()) {
        scale.tare();
        prefs.putLong("tare_offset", scale.get_offset());
      } else {
        scale.set_offset(0);
        prefs.putLong("tare_offset", 0);
      }
    }
    setRgb(0, 0, 0);
  }

  void tare() {
    if (!scale.is_ready()) {
#if HYDRATION_LOG
      Serial.println("[HW] Tare skipped: HX711 not ready (check DT/SCK wiring)");
#endif
      return;
    }
    scale.tare();
    prefs.putLong("tare_offset", scale.get_offset());
#if HYDRATION_LOG
    Serial.println("[HW] Tare OK, offset saved");
#endif
  }

  void stopAll() {
    setLed(false);
    setBuzzer(false);
    setRgb(0, 0, 0);
  }

  float getWeight() {
    if (scale.is_ready())
      lastWeight_ = scale.get_units(1);
    return lastWeight_;
  }

  // --- Baseline weight in NVM (for drinking logic) ---
  bool loadBaseline(float *baselineOut) {
    if (prefs.isKey("baseline_weight")) {
      *baselineOut = prefs.getFloat("baseline_weight", 0.0f);
      return true;
    }
    if (prefs.isKey("last_weight")) {
      *baselineOut = prefs.getFloat("last_weight", 0.0f);
      return true;
    }
    return false;
  }

  void saveBaseline(float baseline) {
    prefs.putFloat("baseline_weight", baseline);
    prefs.remove("last_weight");
  }

  void clearBaseline() {
    prefs.remove("baseline_weight");
    prefs.remove("last_weight");
  }

  // --- Daily total in NVM (for stats / dashboard) ---
  void loadTotals(float *totalOut, int *dayOut) {
    *totalOut = prefs.getFloat("daily_total", 0.0f);
    *dayOut = prefs.getInt("last_day", 0);
  }

  void saveTotals(float total, int day) {
    prefs.putFloat("daily_total", total);
    prefs.putInt("last_day", day);
  }

  void setLed(bool on) { digitalWrite(PIN_LED_WHITE, on ? HIGH : LOW); }
  void setBuzzer(bool on) { digitalWrite(PIN_BUZZER, on ? HIGH : LOW); }

  void setRgb(int colorCode) {
    uint8_t r = 0, g = 0, b = 0;
    switch (colorCode) {
    case 1:
      r = 255;
      break;
    case 2:
      g = 255;
      break;
    case 3:
      b = 255;
      break;
    case 4:
      r = 255;
      g = 255;
      b = 255;
      break;
    case 5:
      r = 255;
      g = 90;
      break;
    case 6:
      g = 32;
      break;
    case 7:
      b = 32;
      break;
    case 8:
      r = 48;
      b = 72;
      break;
    default:
      break;
    }
    setRgb(r, g, b);
  }

  // Raw RGB (0–255). Common-anode: we drive 255-value.
  void setRgb(uint8_t r, uint8_t g, uint8_t b) {
    analogWrite(PIN_RGB_R, 255 - r);
    analogWrite(PIN_RGB_G, 255 - g);
    analogWrite(PIN_RGB_B, 255 - b);
  }

  void setRawRgb(uint8_t r, uint8_t g, uint8_t b) { setRgb(r, g, b); }

  void saveHydrationState(float baseline, float dailyTotal, int day) {
    saveBaseline(baseline);
    saveTotals(dailyTotal, day);
  }

  void loadHydrationState(float *baseline, float *dailyTotal, int *day) {
    if (!loadBaseline(baseline))
      *baseline = 0.0f;
    loadTotals(dailyTotal, day);
  }

  // Rainbow animation; call every loop. speedMs = ms between hue steps.
  void animateRainbow(unsigned long speedMs) {
    static unsigned long lastUpdate = 0;
    static int hue = 0;
    if (millis() - lastUpdate >= speedMs) {
      lastUpdate = millis();
      hue = (hue + 1) % 360;
      float h = hue / 60.0f;
      int i = (int)h;
      float f = h - i;
      int q = (int)(255 * (1 - f));
      int t = (int)(255 * f);
      uint8_t r = 0, g = 0, b = 0;
      switch (i) {
        case 0: r = 255; g = t;   break;
        case 1: r = q;   g = 255; break;
        case 2: g = 255; b = t;   break;
        case 3: g = q;   b = 255; break;
        case 4: r = t;   b = 255; break;
        case 5: r = 255; b = q;   break;
      }
      setRgb(r, g, b);
    }
  }
};

#endif
