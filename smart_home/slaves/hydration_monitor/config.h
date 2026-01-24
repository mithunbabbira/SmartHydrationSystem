#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// ==================== Hardware Pins ====================
// RGB LED (Common Cathode/Anode) - Legacy Pins
#define PIN_RED 27
#define PIN_GREEN 14
#define PIN_BLUE 12
#define PIN_ALERT_LED 25 // White Notification LED

// Buzzer (Active/Passive) - Legacy Pin
#define PIN_BUZZER 26

// Sensor Pins
#define LOADCELL_DOUT_PIN 32
#define LOADCELL_SCK_PIN 33

// ==================== Sensor Calibration ====================
#define CALIBRATION_FACTOR 350.3

// ==================== System Thresholds ====================
#define WEIGHT_MISSING_THRESHOLD -50.0 // Below this, bottle is removed

// ==================== Logic Parameters (Moved from Server)
// ====================
#define DAILY_GOAL_ML 2000
#define DRINK_THRESHOLD_MIN 30.0 // 30g
#define REFILL_THRESHOLD 100.0   // 100g

// Timing
#define WEIGHT_SAMPLE_INTERVAL 500       // 0.5s check
#define TELEMETRY_INTERVAL 5000          // 5s report
#define CHECK_INTERVAL_MS 1800000        // 30 mins hydration check
#define BOTTLE_MISSING_TIMEOUT_MS 180000 // 3 mins

// Alert Patterns
#define ALERT_BLINK_WARNING_MS 1000
#define ALERT_BLINK_CRITICAL_MS 300
#define ALERT_BEEP_DURATION_MS 50
#define ALERT_WAIT_TIME_MS 10000

// Schedule (IST +5:30 handled in Logic)
#define SLEEP_START_HOUR 23 // 11 PM
#define SLEEP_END_HOUR 10   // 10 AM

// ==================== Network Security ====================
const uint8_t PRODUCTION_MASTER_MAC[] = {0xF0, 0x24, 0xF9, 0x0D, 0x90, 0xA4};

#endif // CONFIG_H
