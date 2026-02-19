#ifndef HYDRATION_V3_CONFIG_H
#define HYDRATION_V3_CONFIG_H

// ============== SLEEP TIME (24h format) ==============
// During this window we do not print weight / run reminders.
#define SLEEP_START_HOUR  23   // Sleep from 23:00
#define SLEEP_END_HOUR    10   // Wake at 10:00 (sleep = 23:00–09:59)

// ============== PI / ESP-NOW ==============
#define MASTER_MAC_BYTES  { 0xF0, 0x24, 0xF9, 0x0D, 0x90, 0xA4 }

// ============== PINS ==============
#define PIN_LED_WHITE  25
#define PIN_BUZZER     26
#define PIN_RGB_R      27
#define PIN_RGB_G      14
#define PIN_RGB_B      12
#define PIN_SCALE_DT   32
#define PIN_SCALE_SCK  33

// ============== SCALE ==============
// Negative = load cell gives lower raw values when weight increases (invert sign)
#define CALIBRATION_FACTOR  (-350.3f)

// ============== TIME SYNC ==============
#define TIME_SYNC_TIMEOUT_MS   60000   // Stop requesting time after this
#define TIME_SYNC_REQUEST_MS   5000    // Request time every 5s until we get it

// ============== PRESENCE FLOW HARDENING ==============
#define PRESENCE_REPLY_TIMEOUT_MS       10000   // wait this long for Pi presence reply
#define PRESENCE_RETRY_AFTER_TIMEOUT_MS 60000   // retry reminder check after timeout

// ============== WEIGHT PRINT (when awake) ==============
#define WEIGHT_PRINT_INTERVAL_MS  1000   // Print weight to Serial this often

// ============== BOTTLE / ALERT LOGIC ==============
// If weight < THRESHOLD_WEIGHT, bottle is considered missing.
#define THRESHOLD_WEIGHT           80.0f
#define BOTTLE_HYSTERESIS_G         8.0f
#define BOTTLE_CONFIRM_SAMPLES         3
#define BOTTLE_SAMPLE_INTERVAL_MS    120
// After this delay we start the \"no bottle\" alert (flash + Pi signal)
#define MISSING_ALERT_DELAY_MS   180000   // 3 minutes before triggering missing bottle alert
// #define MISSING_ALERT_DELAY_MS   5000

// After alert is active, buzzer joins after this extra delay
#define MISSING_BUZZER_DELAY_MS  5000    // ms after alert start before buzzer joins
#define BLINK_INTERVAL_MS         500    // LED blink period for missing alert

// Base colors when bottle is present
#define COLOR_DAY_R   128
#define COLOR_DAY_G     0
#define COLOR_DAY_B   255

#define COLOR_SLEEP_R   0
#define COLOR_SLEEP_G   0
#define COLOR_SLEEP_B 255

// Missing-bottle alert color (flashing)
#define COLOR_MISSING_R 255
#define COLOR_MISSING_G   0
#define COLOR_MISSING_B   0

// Weight stabilization time after bottle placed back
#define STABILIZATION_MS          2000    // ms to wait before evaluating weight

// ============== DRINKING LOGIC ==============
// How often (during daytime) we evaluate drink/refill vs last baseline
#define DRINK_CHECK_INTERVAL_MS   1800000  // 30 minutes
// Amount decrease to consider as \"user drank\"
#define DRINK_MIN_DELTA           50.0f
// Amount increase to consider as \"bottle refilled\"
#define REFILL_MIN_DELTA         100.0f
#define BOOT_REBASE_DELTA   REFILL_MIN_DELTA
#define WEIGHT_CONFIRM_DELAY_MS       250
#define WEIGHT_CONFIRM_MAX_DRIFT_G    5.0f
// Drinking reminder: first just color, after this delay buzzer joins
#define DRINK_ALERT_BUZZER_DELAY_MS  5000
// How long (ms) buzzer stays active after it joins the drinking alert
#define DRINK_ALERT_BUZZER_WINDOW_MS 10000
// Drinking alert color (e.g. cyan)
#define COLOR_DRINK_ALERT_R        0
#define COLOR_DRINK_ALERT_G      255
#define COLOR_DRINK_ALERT_B      255

// ============== LOGGING ==============
#define HYDRATION_LOG  1   // 1 = Serial logs, 0 = quiet

#endif
