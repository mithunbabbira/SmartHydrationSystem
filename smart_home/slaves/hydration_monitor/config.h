#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/*
 * Hydration Monitor Configuration (Production)
 * ============================================
 * Hardware: ESP32 + HX711 Load Cell + Buzzer + RGB LED
 */

// ==================== Hardware Pins ====================
// Load Cell (HX711)
// NOTE: These are the working pins from previous successful tests
#define LOADCELL_DOUT_PIN 23
#define LOADCELL_SCK_PIN 22

// RGB LED (Common Cathode/Anode)
#define PIN_RED 5
#define PIN_GREEN 4
#define PIN_BLUE 2

// Buzzer (Active/Passive)
#define PIN_BUZZER 18

// Button (Snooze/Reset) - Currently used for Tare/Snooze command locally?
#define PIN_BTN 19

// ==================== Sensor Calibration ====================
// Calibration factor: Adjust this using a known weight (e.g. 100g object)
// Process: Scale reading / Known weight = Factor
// Value from previous calibration: 420.0 (or 350.3 from old config)
#define CALIBRATION_FACTOR 420.0

// ==================== System Thresholds ====================
// Weight Sensing (Grams)
#define WEIGHT_MISSING_THRESHOLD                                               \
  -50.0                             // Below this, bottle is considered removed
#define DRINK_DETECTION_DELTA -30.0 // Weight drop > 30g is a "drink"

// Timing (Milliseconds)
#define WEIGHT_SAMPLE_INTERVAL 500 // Read weight every 0.5s
#define TELEMETRY_INTERVAL 5000    // Send data every 5s
#define HEARTBEAT_INTERVAL 10000   // Keep-alive every 10s

// Alert Patterns
#define ALERT_BLINK_WARNING_MS 1000 // Slow blink for Level 1
#define ALERT_BLINK_CRITICAL_MS 300 // Fast blink for Level 2
#define ALERT_BEEP_DURATION_MS 50   // Short beep length

// ==================== Network Security ====================
// Master Gateway MAC Address (Production)
// Used to verify commands come from the trusted Gateway
const uint8_t PRODUCTION_MASTER_MAC[] = {0xF0, 0x24, 0xF9, 0x0D, 0x90, 0xA4};

#endif // CONFIG_H
