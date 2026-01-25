#ifndef HARDWARE_H
#define HARDWARE_H

#include "HX711.h"
#include <Arduino.h>
#include <Preferences.h>

// --- Pin Definitions ---
#define PIN_LED_WHITE 25
#define PIN_BUZZER 26
#define PIN_RGB_R 27
#define PIN_RGB_G 14
#define PIN_RGB_B 12
#define PIN_SCALE_DT 32
#define PIN_SCALE_SCK 33

#define CALIBRATION_FACTOR 350.3

class HydrationHW {
private:
  HX711 scale;
  Preferences prefs;

public:
  void begin() {
    pinMode(PIN_LED_WHITE, OUTPUT);
    digitalWrite(PIN_LED_WHITE, LOW); // Force OFF immediately
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    pinMode(PIN_RGB_R, OUTPUT); // Common Anode: LOW=ON
    pinMode(PIN_RGB_G, OUTPUT);
    pinMode(PIN_RGB_B, OUTPUT);

    // Init Scale & NVM
    scale.begin(PIN_SCALE_DT, PIN_SCALE_SCK);
    scale.set_scale(CALIBRATION_FACTOR);

    prefs.begin("hydration", false); // Namespace "hydration", RW
    float savedOffset = prefs.getFloat("offset", 0.0);

    if (savedOffset != 0.0) {
      scale.set_offset(savedOffset);
      // Serial.println("Loaded Tare Offset from NVM");
    } else {
      scale.tare(); // InitialTare if no saved value
      // Serial.println("No saved Tare - Zeroing now");
    }

    stopAll();
  }

  void tare() {
    scale.tare();
    float newOffset = scale.get_offset();
    prefs.putFloat("offset", newOffset);
    // Serial.println("Tare Saved to NVM");
  }

  void stopAll() {
    setLed(false);
    setBuzzer(false);
    setRgb(0); // Off
  }

  void setLed(bool on) {
    Serial.print("DEBUG: setLed called with ");
    Serial.println(on ? "HIGH" : "LOW");
    digitalWrite(PIN_LED_WHITE, on ? HIGH : LOW);
  }

  void setBuzzer(bool on) { digitalWrite(PIN_BUZZER, on ? HIGH : LOW); }

  // 0=Off, 1=Red, 2=Green, 3=Blue, 4=White
  void setRgb(int colorCode) {
    // Common Anode: 255 (Max) is OFF, 0 is ON
    int r = 255, g = 255, b = 255;

    switch (colorCode) {
    case 1:
      r = 0;
      break; // Red
    case 2:
      g = 0;
      break; // Green
    case 3:
      b = 0;
      break; // Blue
    case 4:
      r = 0;
      g = 0;
      b = 0;
      break; // White
    default:
      break; // Off
    }

    analogWrite(PIN_RGB_R, r);
    analogWrite(PIN_RGB_G, g);
    analogWrite(PIN_RGB_B, b);
  }

  float getWeight() {
    if (scale.is_ready()) {
      // Return average of 3 readings for stability
      // Note: Returns UNITS (grams) due to set_scale
      return (float)scale.get_units(3);
    }
    return -1.0;
  }
};

#endif
