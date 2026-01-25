#ifndef SLAVE_CONFIG_H
#define SLAVE_CONFIG_H

// --- Timing Configuration ---
#define MISSING_TIMEOUT_MS 10000   // 10s: Time bottle must be gone before alert
#define BUZZER_START_DELAY_MS 5000 // 5s:  Delay before buzzer joins LED alert
#define STABILIZATION_MS 2000      // 2s:  Wait time after bottle replaced
//#define CHECK_INTERVAL_MS 1800000  // 30m: Interval between hydration checks
#define CHECK_INTERVAL_MS 20000 
#define LED_ALERT_DURATION 10000   // 10s: Duration of Pre-Alert blink
#define AWAY_CHECK_INTERVAL_MS 60000 // 1m:  Check presence during alert

// --- Sleep Schedule ---
#define SLEEP_START_HOUR 23 // 11 PM
#define SLEEP_END_HOUR 1   // 10 AM

// --- Weight Thresholds ---
#define THRESHOLD_WEIGHT 80.0 // Min weight to detect bottle presence
#define DRINK_MIN_ML 50.0     // Min drop to count as drink
#define REFILL_MIN_ML 100.0   // Min gain to count as refill

// --- Status Colors (RGB IDs) ---
// 0=Off, 1=Red, 2=Green, 3=Blue, 4=White, 5=Orange
#define COLOR_ALERT 1  // Red
#define COLOR_OK 2     // Green (Drink confirmed)
#define COLOR_REFILL 3 // Blue (Refill confirmed)
#define COLOR_IDLE 6   // Dim Green (System Active/Day)
#define COLOR_SLEEP 7  // Dim Blue (Night Mode)

#endif
