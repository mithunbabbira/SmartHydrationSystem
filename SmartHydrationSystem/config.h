/*
 * Smart Hydration Reminder System - Configuration
 * ================================================
 *
 * IMPORTANT: Update these values for your setup! mii
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// ==================== WiFi Configuration ====================
#define WIFI_SSID "No 303"         // Replace with your WiFi name
#define WIFI_PASSWORD "3.14159265" // Replace with your WiFi password

// ==================== Time Configuration (NTP) ====================
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 19800 // IST: GMT +5.5 (5.5 * 3600)
#define DAYLIGHT_OFFSET_SEC 0

// ==================== MQTT Configuration ====================
#define MQTT_SERVER "raspberrypi.local" // Replace with your Pi5 IP address
#define MQTT_PORT 1883
// #define MQTT_USER "babbira"        // Not needed - anonymous access enabled
// #define MQTT_PASSWORD "3.14159265" // Not needed - anonymous access enabled
#define MQTT_CLIENT_ID "ESP32_Hydration"

// ==================== Hardware Pins ====================
// Load Cell (HX711)
#define HX711_DOUT_PIN 32
#define HX711_SCK_PIN 33
#define CALIBRATION_FACTOR 350.3 // Calibrated with 218g powerbank

// LEDs
#define LED_NOTIFICATION_PIN 25 // White LED for alerts
#define RGB_RED_PIN 27
#define RGB_GREEN_PIN 14
#define RGB_BLUE_PIN 12

// Buzzer
#define BUZZER_PIN 26

// Button
#define SNOOZE_BUTTON_PIN 13

// ==================== System Parameters ====================
// Timing
#define CHECK_INTERVAL_MS (30 * 60 * 1000) // 30 minutes in milliseconds


// #define CHECK_INTERVAL_MS (1 * 20 * 1000) // 30 minutes in milliseconds


#define TELEMETRY_INTERVAL_MS (30 * 1000) // 30 seconds for live updates
#define SLEEP_START_HOUR 23               // 11 PM
#define SLEEP_END_HOUR 10                 // 10 AM
#define REFILL_CHECK_HOUR 12              // 12 PM (noon)

// Weight thresholds
#define DRINK_THRESHOLD_MIN 90       // Minimum drink: 90g
#define REFILL_THRESHOLD 100         // Minimum refill: 100g
#define REFILL_CHECK_MIN_WEIGHT 1500 // Daily check: bottle must be ≥1.5kg
#define PICKUP_THRESHOLD 50 // Bottle considered "removed" below this weight

// Alert durations
#define LED_ALERT_DURATION 10000      // 10 seconds (White LED)
#define BUZZER_ALERT_DURATION 10000   // 10 seconds
#define ALERT_WAIT_TIME 10000         // 10 seconds between escalations
#define ALERT_RETRY_INTERVAL_MS 10000 // 10 seconds retry if no drink detected
#define BOTTLE_MISSING_TIMEOUT_MS                                              \
  180000 // 3 minutes missing bottle timeout (3 * 60 * 1000 = 180000)

// Snooze
#define SNOOZE_DURATION_MS (5 * 60 * 1000) // 5 minutes
#define MAX_CONSECUTIVE_SNOOZES 3

// Presence Detection (WiFi MAC - most reliable!)
#define PRESENCE_CHECK_INTERVAL 10         // Check every 10 seconds
#define PRESENCE_TIMEOUT_COUNT 30          // 30 failed checks = away (5 min)
#define PHONE_WIFI_MAC "48:EF:1C:49:6A:E8" // Your phone's WiFi MAC address

// Misc
#define SERIAL_BAUD_RATE 115200
#define WEIGHT_READING_SAMPLES 10   // Average of 10 readings
#define DAILY_GOAL_ML 2000          // 2L daily goal
#define STOP_ALERTS_AFTER_GOAL true // Stop reminders if goal is reached

// ==================== MQTT Topics ====================
#define TOPIC_STATUS_ONLINE "hydration/status/online"
#define TOPIC_STATUS_BT "hydration/status/bluetooth"
#define TOPIC_STATUS_MODE "hydration/status/mode"

#define TOPIC_WEIGHT_CURRENT "hydration/weight/current"
#define TOPIC_WEIGHT_PREVIOUS "hydration/weight/previous"
#define TOPIC_WEIGHT_DELTA "hydration/weight/delta"

#define TOPIC_CONSUMPTION_LAST "hydration/consumption/last_drink"
#define TOPIC_CONSUMPTION_INTERVAL "hydration/consumption/interval_ml"
#define TOPIC_CONSUMPTION_TODAY "hydration/consumption/today_ml"

#define TOPIC_ALERTS_LEVEL "hydration/alerts/level"
#define TOPIC_ALERTS_TRIGGERED "hydration/alerts/triggered"
#define TOPIC_ALERTS_BOTTLE_MISSING "hydration/alerts/bottle_missing"
#define TOPIC_ALERTS_SNOOZE "hydration/alerts/snooze_active"
#define TOPIC_ALERTS_REFILL_CHECK "hydration/alerts/daily_refill_check"

// Command topics (ESP32 subscribes to these)
#define TOPIC_CMD_TARE "hydration/commands/tare_scale"
#define TOPIC_CMD_LED "hydration/commands/trigger_led"
#define TOPIC_CMD_BUZZER "hydration/commands/trigger_buzzer"
#define TOPIC_CMD_RGB "hydration/commands/trigger_rgb"
#define TOPIC_CMD_SNOOZE "hydration/commands/snooze"
#define TOPIC_CMD_REBOOT "hydration/commands/reboot"
#define TOPIC_CMD_RESET_CONSUMPTION "hydration/commands/reset_today"

// ==================== RGB Colors (0-255) ====================
struct RGBColor {
  uint8_t r, g, b;
};

const RGBColor RGB_OFF = {0, 0, 0};
const RGBColor RGB_RED = {255, 0, 0};
const RGBColor RGB_GREEN = {0, 255, 0};
const RGBColor RGB_BLUE = {0, 0, 255};
const RGBColor RGB_YELLOW = {255, 255, 0};
const RGBColor RGB_ORANGE = {255, 165, 0};
const RGBColor RGB_PURPLE = {128, 0, 128};
const RGBColor RGB_WHITE = {255, 255, 255};
const RGBColor RGB_CYAN = {0, 255, 255};
const RGBColor RGB_MAGENTA = {255, 0, 255};

#endif // CONFIG_H
