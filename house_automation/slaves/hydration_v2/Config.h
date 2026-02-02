#ifndef HYDRATION_V2_CONFIG_H
#define HYDRATION_V2_CONFIG_H

// All v1 features: no-bottle alert (0x50), drinking reminder (0x52), drink/refill detection,
// presence, sleep window, daily reset. See HYDRATION_V2_PLAN.md §6.1 for parity checklist.

// --- Logging: 1 = timestamped Serial, 0 = no Serial log (production) ---
#define HYDRATION_LOG 1

// --- Master MAC (must match Master ESP32 that talks to Pi) ---
#define MASTER_MAC_BYTES { 0xF0, 0x24, 0xF9, 0x0D, 0x90, 0xA4 }

// --- Pins ---
#define PIN_LED_WHITE 25
#define PIN_BUZZER 26
#define PIN_RGB_R 27
#define PIN_RGB_G 14
#define PIN_RGB_B 12
#define PIN_SCALE_DT 32
#define PIN_SCALE_SCK 33
#define CALIBRATION_FACTOR 350.3f

// --- Time sync (non-blocking) ---
#define TIME_SYNC_TIMEOUT_MS 60000
#define TIME_SYNC_REQUEST_MS 5000

// --- Timing ---
#define MISSING_TIMEOUT_MS 180000
#define BUZZER_START_DELAY_MS 5000
#define STABILIZATION_MS 2000
#define CHECK_INTERVAL_MS 1800000
#define LED_ALERT_DURATION 10000
#define AWAY_CHECK_INTERVAL_MS 60000
#define PRESENCE_TIMEOUT_MS 10000
#define BLINK_INTERVAL_MS 500

// --- Sleep ---
#define SLEEP_START_HOUR 23
#define SLEEP_END_HOUR 10

// --- Weight ---
#define THRESHOLD_WEIGHT 80.0f
#define DRINK_MIN_ML 50.0f
#define REFILL_MIN_ML 100.0f
#define DRINK_CONFIRM_MS 400

// --- Colors (RGB IDs) ---
#define COLOR_ALERT 1
#define COLOR_OK 2
#define COLOR_REFILL 3
#define COLOR_IDLE 8
#define COLOR_SLEEP 7

#endif
