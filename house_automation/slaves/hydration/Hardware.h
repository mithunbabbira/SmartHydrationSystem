#ifndef HARDWARE_H
#define HARDWARE_H

#include "HX711.h"
#include <Arduino.h>

// --- Pin Definitions ---
#define PIN_LED_WHITE 25
#define PIN_BUZZER 26
#define PIN_RGB_R 27
#define PIN_RGB_G 14
#define PIN_RGB_B 12
#define PIN_SCALE_DT 32
#define PIN_SCALE_SCK 33

class HydrationHW {
private:
  HX711 scale;

public:
  void begin() {
    pinMode(PIN_LED_WHITE, OUTPUT);
    digitalWrite(PIN_LED_WHITE, LOW); // Force OFF immediately
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    pinMode(PIN_RGB_R, OUTPUT); // Common Anode: LOW=ON
    pinMode(PIN_RGB_G, OUTPUT);
    pinMode(PIN_RGB_B, OUTPUT);

    // Init Scale
    scale.begin(PIN_SCALE_DT, PIN_SCALE_SCK);

    stopAll();
  }

  void stopAll() {
    setLed(false);
    setBuzzer(false);
    setRgb(0); // Off
  }

  void setLed(bool on) { digitalWrite(PIN_LED_WHITE, on ? HIGH : LOW); }

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
      // Note: Returns raw value for now unless calibrated
      return (float)scale.read_average(3);
    }
    return -1.0;
  }
};

#endif
