#ifndef HYDRATION_V2_HARDWARE_H
#define HYDRATION_V2_HARDWARE_H

#include "Config.h"
#include "Log.h"
#include "HX711.h"
#include <Arduino.h>
#include <Preferences.h>

// NVM (Preferences namespace "hydration"):
// - tare_offset (long): scale zero offset. Load at begin(); save on tare() or first tare in begin().
// - last_weight, daily_total, last_day: hydration state. Load in StateMachine::begin(); save in
//   evaluateWeightChange() (after bottle return) and in checkDay() (daily reset).
class HydrationHW {
  HX711 scale;
  Preferences prefs;
  float lastWeight = 0.0f;

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
      scale.tare();
      prefs.putLong("tare_offset", scale.get_offset());
    }
    stopAll();
  }

  void tare() {
    scale.tare();
    prefs.putLong("tare_offset", scale.get_offset());
  }

  void stopAll() {
    setLed(false);
    setBuzzer(false);
    setRgb(0);
  }

  void setLed(bool on) { digitalWrite(PIN_LED_WHITE, on ? HIGH : LOW); }
  void setBuzzer(bool on) { digitalWrite(PIN_BUZZER, on ? HIGH : LOW); }

  void setRgb(int colorCode) {
    int r = 255, g = 255, b = 255;
    switch (colorCode) {
      case 0: r = 0; g = 0; b = 0; break;
      case 1: r = 0; break;
      case 2: g = 0; break;
      case 3: b = 0; break;
      case 4: r = 0; g = 0; b = 0; break;
      case 5: r = 0; g = 165; b = 255; break;
      case 6: g = 250; break;
      case 7: b = 250; break;
      case 8: r = 220; g = 255; b = 220; break;
      default: break;
    }
    analogWrite(PIN_RGB_R, r);
    analogWrite(PIN_RGB_G, g);
    analogWrite(PIN_RGB_B, b);
  }

  float getWeight() {
    if (scale.is_ready())
      lastWeight = scale.get_units(1);
    return lastWeight;
  }

  void saveHydrationState(float weight, float dailyTotal, int day) {
    prefs.putFloat("last_weight", weight);
    prefs.putFloat("daily_total", dailyTotal);
    prefs.putInt("last_day", day);
  }

  void loadHydrationState(float *weight, float *dailyTotal, int *day) {
    *weight = prefs.getFloat("last_weight", 0.0f);
    *dailyTotal = prefs.getFloat("daily_total", 0.0f);
    *day = prefs.getInt("last_day", 0);
  }

  void setRawRgb(uint8_t r, uint8_t g, uint8_t b) {
    analogWrite(PIN_RGB_R, 255 - r);
    analogWrite(PIN_RGB_G, 255 - g);
    analogWrite(PIN_RGB_B, 255 - b);
  }

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
        case 0: r = 255; g = t; break;
        case 1: r = q; g = 255; break;
        case 2: g = 255; b = t; break;
        case 3: g = q; b = 255; break;
        case 4: r = t; b = 255; break;
        case 5: r = 255; b = q; break;
      }
      setRawRgb(r, g, b);
    }
  }
};

#endif
