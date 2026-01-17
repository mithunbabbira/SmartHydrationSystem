/*
 * =====================================================
 * Smart Hydration Reminder System
 * =====================================================
 *
 * ESP32-based water bottle monitoring with:
 * - Weight tracking (HX711 + 5kg load cell)
 * - BLE presence detection
 * - Escalating alerts (LED → Buzzer → MQTT)
 * - RGB status indicator
 * - Daily refill check
 * - Snooze button
 * - MQTT integration with Pi5
 *
 * Author: Babbira
 * Version: 1.0
 */

#include "HX711.h"
#include "config.h"
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>

// ==================== Global Objects ====================
HX711 scale;
WiFiClient espClient;
PubSubClient mqtt(espClient);
Preferences prefs;

// ==================== State Variables ====================
// Weight tracking
float currentWeight = 0;
float previousWeight = 0;
float weightDelta = 0;

// Timing
unsigned long lastCheckTime = 0;
unsigned long lastPresenceCheckTime = 0;
unsigned long lastTelemetryTime = 0;
unsigned long snoozeUntil = 0;
float liveWeight = 0; // Globally updated once per loop

// Consumption tracking
float todayConsumptionML = 0;
int lastResetDay = 0;
struct tm lastDrinkTime;

// System state
enum SystemMode {
  MODE_INITIALIZING,
  MODE_MONITORING,
  MODE_SLEEPING,
  MODE_AWAY,
  MODE_SNOOZED,
  MODE_ALERTING,
  MODE_PICKED_UP,
  MODE_EVALUATING,
  MODE_BOTTLE_MISSING
};
SystemMode currentMode = MODE_INITIALIZING;

// Alert state
int currentAlertLevel = 0; // 0=none, 1=LED, 2=Buzzer, 3=MQTT
unsigned long alertStartTime = 0;
float preAlertWeight = 0;
unsigned long nextAllowedAlertTime = 0;
unsigned long lastBottlePresentTime = 0;
unsigned long lastBottleReplacedTime = 0;

// Flags
bool phonePresent = false;
bool dailyRefillCheckDone = false;
int presenceFailCount = 0;
int consecutiveSnoozeCount = 0;

// Button debouncing
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 200;

// ==================== Function Prototypes ====================
void setupWiFi();
void setupMQTT();
void setupHardware();
void reconnectMQTT();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void publishTelemetry();
void checkWeight(float currentWeight);
void checkPhonePresence(); // WiFi-based presence detection
void checkDailyRefillReminder();
void handleSnoozeButton();
void escalateAlert();
void setRGB(RGBColor color);
void setRGBPulse(RGBColor color);
void triggerLED(int duration);
void triggerBuzzer(int duration);
void handleAlertState();
void evaluateDrinking();
void saveScaleOffset();
bool loadScaleOffset();
void saveConsumption();
void loadConsumption();
void syncHardware();
String getTimeString();
int getCurrentHour();

// ==================== Setup ====================
void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  delay(1000);

  Serial.println("\n\n========================================");
  Serial.println("  Smart Hydration Reminder System");
  Serial.println("========================================\n");

  // Initialize hardware
  setupHardware();

  // Initialize WiFi
  setupWiFi();

  // Initialize MQTT
  setupMQTT();

  // Ensure lastBottlePresentTime is initialized
  lastBottlePresentTime = millis();

  // Initialize HX711
  Serial.println("Initializing HX711...");
  scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
  scale.set_scale(CALIBRATION_FACTOR);

  if (loadScaleOffset()) {
    Serial.println("✓ Loaded saved tare from NVM");
  } else {
    Serial.println("⚠ No saved tare found! Taring scale... Remove all weight!");
    delay(5000);
    scale.tare();
    saveScaleOffset();
    Serial.println("✓ Scale calibrated and saved to NVM");
  }

  // Set initial RGB color
  setRGB(RGB_BLUE);

  currentMode = MODE_MONITORING;
  lastCheckTime = millis();

  // Load consumption and handle daily reset
  loadConsumption();

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    if (timeinfo.tm_mday != lastResetDay) {
      Serial.printf("[INFO] 🌅 New day detected! Resetting consumption. (Prev "
                    "Day: %d, Current: %d)\n",
                    lastResetDay, timeinfo.tm_mday);
      todayConsumptionML = 0;
      lastResetDay = timeinfo.tm_mday;
      saveConsumption();
    }
  }

  // Final Boot Diagnostics
  Serial.println("\n--- [SYSTEM DIAGNOSTICS] ---");
  Serial.print("[INFO] IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("[INFO] Active Mode: ");
  Serial.println(currentMode);
  Serial.print("[INFO] Consumption: ");
  Serial.print(todayConsumptionML);
  Serial.println(" ml");
  Serial.print("[INFO] Reset Day: ");
  Serial.println(lastResetDay);
  Serial.printf("[INFO] Monitoring Hours: %02d:00 - %02d:00\n", SLEEP_END_HOUR,
                SLEEP_START_HOUR);
  Serial.println("----------------------------\n");
  Serial.println("[INFO] ✓✓✓ System Ready ✓✓✓\n");
}

// ==================== Main Loop ====================
void loop() {
  static unsigned long loopCount = 0;
  static unsigned long lastLoopReport = 0;
  unsigned long loopStart = millis();
  loopCount++;

  // Report loop frequency every 60 seconds (Reduced Noise)
  if (millis() - lastLoopReport >= 60000) {
    if (loopCount > 0) {
      Serial.printf(
          "[INFO] System Heartbeat | Loop: %lu/60s (avg: %lums/loop)\n",
          loopCount, 60000 / loopCount);
    }
    loopCount = 0;
    lastLoopReport = millis();
  }

  // Maintain MQTT connection - GLOBAL THROTTLING to prevent 5s lag
  static unsigned long lastMqttAttempt = 0;
  if (!mqtt.connected()) {
    // Only try reconnecting every 60 seconds if it's failing
    if (millis() - lastMqttAttempt >= 60000) {
      reconnectMQTT();
      lastMqttAttempt = millis();
    }
  }
  mqtt.loop();

  // Check snooze button
  handleSnoozeButton();

  // Ensure mode is updated based on time (for Sleep Mode)
  int hour = getCurrentHour();
  if (hour >= SLEEP_START_HOUR || hour < SLEEP_END_HOUR) {
    currentMode = MODE_SLEEPING;
  } else if (currentMode == MODE_SLEEPING) {
    currentMode = MODE_MONITORING;
  }

  // Midnight Reset Logic
  static unsigned long lastDayCheck = 0;
  if (millis() - lastDayCheck > 60000) { // Check every minute
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      if (timeinfo.tm_mday != lastResetDay) {
        Serial.println(
            "[INFO] 🌅 Midnight reset triggered! Starting fresh for the day.");
        todayConsumptionML = 0;
        lastResetDay = timeinfo.tm_mday;
        saveConsumption();

        if (mqtt.connected()) {
          mqtt.publish(TOPIC_CONSUMPTION_TODAY, "0");
          mqtt.publish("hydration/status/message",
                       "Daily consumption reset at midnight");
        }
      }
    }
    lastDayCheck = millis();
  }

  // Read scale once per loop for consistency (with small timeout)
  static bool bottleWasOff = false;
  if (scale.wait_ready_timeout(1)) {
    liveWeight = scale.get_units(1); // Single sample for responsive loop

    // Update last bottle presence time if bottle is on scale
    if (liveWeight >= PICKUP_THRESHOLD) {
      if (bottleWasOff) {
        Serial.println(
            "[INFO] ✓ Bottle detected on scale - starting 2s stabilization");
        lastBottleReplacedTime = millis();
        bottleWasOff = false;

        if (currentMode == MODE_BOTTLE_MISSING) {
          Serial.println("[INFO] ✓ Bottle replaced - clearing missing alert");
          currentMode = MODE_MONITORING;
          mqtt.publish(TOPIC_ALERTS_BOTTLE_MISSING, "cleared");
        }
      }
      lastBottlePresentTime = millis();
    } else {
      bottleWasOff = true;
    }
  } else {
    // If scale isn't ready, we don't update liveWeight,
    // but we check if the bottle was already off
    if (liveWeight < PICKUP_THRESHOLD) {
      bottleWasOff = true;
    }
  }

  // GLOBAL BOTTLE MISSING CHECK (Ungated from scale hardware timing)
  if (bottleWasOff || liveWeight < PICKUP_THRESHOLD) {
    if (millis() - lastBottlePresentTime > BOTTLE_MISSING_TIMEOUT_MS) {
      if (currentMode != MODE_BOTTLE_MISSING) {
        Serial.printf(
            "[ALERT] ⚠ CRITICAL: Bottle missing for over %lu seconds!\n",
            BOTTLE_MISSING_TIMEOUT_MS / 1000);
        currentMode = MODE_BOTTLE_MISSING;
        mqtt.publish(TOPIC_ALERTS_BOTTLE_MISSING, "active");
      }
    }
  }

  // Handle alert states - HIGHEST PRIORITY
  if (currentMode == MODE_ALERTING || currentMode == MODE_PICKED_UP ||
      currentMode == MODE_EVALUATING) {
    unsigned long alertHandleStart = millis();
    handleAlertState(); // Now uses global liveWeight
    unsigned long alertHandleTime = millis() - alertHandleStart;
    if (alertHandleTime >
        4000) { // Increased to 4s as evaluateDrinking is intentionaly slow
      Serial.printf("[PERF WARNING] handleAlertState took %lums\n",
                    alertHandleTime);
    }
  } else {
    // Ensure LED is OFF when not alerting
    if (digitalRead(LED_NOTIFICATION_PIN) == HIGH &&
        currentMode != MODE_BOTTLE_MISSING) {
      digitalWrite(LED_NOTIFICATION_PIN, LOW);
    }

    // Normal monitoring logic
    // Check phone presence via WiFi ping (every 10 seconds)
    if (millis() - lastPresenceCheckTime >= PRESENCE_CHECK_INTERVAL * 1000) {
      checkPhonePresence();
      lastPresenceCheckTime = millis();
    }

    // Check daily refill reminder (once at 12 PM)
    checkDailyRefillReminder();

    // Main weight check - ONLY run if bottle is present, for at least 2 seconds
    if (liveWeight >= PICKUP_THRESHOLD && currentMode != MODE_BOTTLE_MISSING &&
        (millis() - lastBottleReplacedTime > 2000)) {
      if (millis() - lastCheckTime >= CHECK_INTERVAL_MS) {
        if (millis() >= nextAllowedAlertTime) {
          checkWeight(liveWeight); // Pass the already-read weight
          lastCheckTime = millis();
        } else {
          Serial.printf(
              "[INFO] ⏳ Waiting for retry interval (%lds remaining)\n",
              (nextAllowedAlertTime - millis()) / 1000);
        }
      }
    }
  }

  // Live telemetry update
  if (millis() - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
    publishTelemetry();
    lastTelemetryTime = millis();
  }

  // Ensure hardware matches current state
  syncHardware();

  delay(10);
}

// ==================== Hardware Setup ====================
void setupHardware() {
  Serial.println("[INFO] Setting up hardware...");

  // LED pins
  pinMode(LED_NOTIFICATION_PIN, OUTPUT);
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);

  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT);

  // Snooze button (with internal pullup)
  pinMode(SNOOZE_BUTTON_PIN, INPUT_PULLUP);

  // Turn off all outputs
  digitalWrite(LED_NOTIFICATION_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  setRGB(RGB_OFF);

  Serial.println("[INFO] ✓ Hardware initialized");
}

// ==================== WiFi Setup ====================
void setupWiFi() {
  Serial.print("[INFO] Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[INFO] ✓ WiFi connected!");
    Serial.print("[INFO] IP Address: ");
    Serial.println(WiFi.localIP());

    // Initialize NTP time sync with multiple servers for reliability
    Serial.println("[INFO] Synchronizing time via NTP...");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org",
               "time.google.com");

    // Wait for time to be set (year must be > 2020)
    struct tm timeinfo;
    int retry = 0;
    while (retry < 15) {
      if (getLocalTime(&timeinfo) && timeinfo.tm_year + 1900 > 2020) {
        break;
      }
      Serial.print(".");
      delay(1000);
      retry++;
    }

    if (timeinfo.tm_year + 1900 > 2020) {
      Serial.println("\n[INFO] ✓ Time synchronized via NTP");
      Serial.println("[INFO] Current Time: " + getTimeString());
    } else {
      Serial.println("\n[WARN] ⚠ NTP sync timed out - clock may be wrong!");
    }
  } else {
    Serial.println("\n[ERROR] ✗ WiFi connection failed!");
  }
}

// ==================== MQTT Setup ====================
void setupMQTT() {
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  reconnectMQTT();
}

void reconnectMQTT() {
  // Skip if no WiFi
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  // Only try once (non-blocking)
  if (!mqtt.connected()) {
    Serial.print("[INFO] Connecting to MQTT broker...");

    // Connect without authentication (anonymous mode)
    if (mqtt.connect(MQTT_CLIENT_ID, TOPIC_STATUS_ONLINE, 0, true, "false")) {
      Serial.println(" ✓ Connected!");

      // Publish online status
      mqtt.publish(TOPIC_STATUS_ONLINE, "true", true);

      // Subscribe to command topics
      mqtt.subscribe(TOPIC_CMD_TARE);
      mqtt.subscribe(TOPIC_CMD_LED);
      mqtt.subscribe(TOPIC_CMD_BUZZER);
      mqtt.subscribe(TOPIC_CMD_RGB);
      mqtt.subscribe(TOPIC_CMD_SNOOZE);
      mqtt.subscribe(TOPIC_CMD_REBOOT);
      mqtt.subscribe(TOPIC_CMD_RESET_CONSUMPTION);

      Serial.println("[INFO] ✓ Subscribed to command topics");
    } else {
      Serial.print("[ERROR] ✗ Failed, rc=");
      Serial.println(mqtt.state());
      Serial.println("[WARN] ⚠ System will continue without MQTT");
    }
  }
}

// ==================== MQTT Callback ====================
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("[MQTT] Received [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  // Handle commands
  if (strcmp(topic, TOPIC_CMD_TARE) == 0 && message == "execute") {
    Serial.println("[CMD] Remote tare requested!");
    scale.tare();
    saveScaleOffset(); // Persist the new tare
    mqtt.publish("hydration/status/message", "Scale tared and saved to NVM");
  } else if (strcmp(topic, TOPIC_CMD_LED) == 0) {
    if (message == "on") {
      digitalWrite(LED_NOTIFICATION_PIN, HIGH);
      delay(2000); // Manual override is still blocking/direct
      digitalWrite(LED_NOTIFICATION_PIN, LOW);
    }
  } else if (strcmp(topic, TOPIC_CMD_BUZZER) == 0) {
    if (message == "on") {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(1000);
      digitalWrite(BUZZER_PIN, LOW);
    }
  } else if (strcmp(topic, TOPIC_CMD_SNOOZE) == 0) {
    int minutes = message.toInt();
    if (minutes > 0) {
      snoozeUntil = millis() + (minutes * 60 * 1000);
      mqtt.publish(TOPIC_ALERTS_SNOOZE, "true");
      Serial.printf("[CMD] Snooze activated for %d minutes\n", minutes);

      // Clear any active alert
      if (currentMode == MODE_ALERTING || currentMode == MODE_PICKED_UP ||
          currentMode == MODE_EVALUATING) {
        currentMode = MODE_MONITORING;
        currentAlertLevel = 0;
        // Hardware will be synced in next loop
      }
    }
  } else if (strcmp(topic, TOPIC_CMD_REBOOT) == 0 && message == "execute") {
    Serial.println("[CMD] Reboot requested!");
    ESP.restart();
  } else if (strcmp(topic, TOPIC_CMD_RESET_CONSUMPTION) == 0 &&
             message == "execute") {
    Serial.println("[CMD] Manual consumption reset triggered!");
    todayConsumptionML = 0;
    saveConsumption();
    mqtt.publish(TOPIC_CONSUMPTION_TODAY, "0");
    mqtt.publish("hydration/status/message",
                 "Daily consumption manually reset");
  }
}

// ==================== Check Phone Presence (Simplified) ====================
void checkPhonePresence() {
  // Simple approach: If ESP32 stays connected to WiFi, assume user is home
  // This is reliable for home use - if you're away, your home WiFi isn't
  // accessible

  if (WiFi.status() == WL_CONNECTED) {
    if (!phonePresent) {
      phonePresent = true;
      presenceFailCount = 0;
      mqtt.publish(TOPIC_STATUS_BT, "connected");
      Serial.println("[PRESENCE] ✓ User at home (WiFi connected)");
    }
    presenceFailCount = 0; // Reset on success
  } else {
    presenceFailCount++;

    if (presenceFailCount >= PRESENCE_TIMEOUT_COUNT && phonePresent) {
      phonePresent = false;
      mqtt.publish(TOPIC_STATUS_BT, "disconnected");
      Serial.println("[PRESENCE] ✗ User away (WiFi disconnected)");
    }
  }
}

// ==================== Weight Check ====================
void checkWeight(float weightAtCheck) {
  // Skip if snoozed
  if (millis() < snoozeUntil) {
    Serial.println("[INFO] ⏸ Snoozed - skipping check");
    return;
  }

  // Skip if sleeping hours
  int hour = getCurrentHour();
  if (hour >= SLEEP_START_HOUR || hour < SLEEP_END_HOUR) {
    currentMode = MODE_SLEEPING;
    Serial.println("[INFO] 😴 Sleep mode active");
    return;
  }

  // Skip if user away
  if (!phonePresent) {
    currentMode = MODE_AWAY;
    Serial.println("[INFO] 🚶 User away - skipping check");
    return;
  }

  currentMode = MODE_MONITORING;

  // Use the weight passed from the fast loop-level reading
  previousWeight = currentWeight;
  currentWeight = weightAtCheck;
  weightDelta = currentWeight - previousWeight;

  Serial.printf(
      "[INFO] Weight Check | Current: %.1fg | Previous: %.1fg | Delta: %.1fg\n",
      currentWeight, previousWeight, weightDelta);

  // Publish telemetry
  publishTelemetry();

  // Check for drinking
  if (weightDelta <= -DRINK_THRESHOLD_MIN) {
    Serial.println("[INFO] ✓ User drank water!");
    setRGB(RGB_GREEN);
    delay(3000);

    todayConsumptionML += abs(weightDelta);
    saveConsumption();
    mqtt.publish(TOPIC_CONSUMPTION_LAST, getTimeString().c_str());
    mqtt.publish(TOPIC_CONSUMPTION_INTERVAL, String(abs(weightDelta)).c_str());
    mqtt.publish(TOPIC_CONSUMPTION_TODAY, String(todayConsumptionML).c_str());

    consecutiveSnoozeCount = 0;
  }
  // Check for refill
  else if (weightDelta >= REFILL_THRESHOLD) {
    Serial.println("[INFO] 🔄 Bottle refilled!");
    setRGB(RGB_CYAN);
    delay(2000);
  }
  // No drinking - but check if goal is already hit
  else if (STOP_ALERTS_AFTER_GOAL && todayConsumptionML >= DAILY_GOAL_ML) {
    Serial.printf(
        "[INFO] Daily goal reached (%.1fml) - skipping drink reminder.\n",
        todayConsumptionML);
    lastCheckTime = millis(); // Reset timer to avoid constant logging
  }
  // No significant change - trigger alert
  else {
    Serial.println("[ALERT] ⚠ No drinking detected - entering alert mode");
    preAlertWeight = currentWeight;
    currentMode = MODE_ALERTING;
    alertStartTime = millis();
    escalateAlert();
  }
}

// ==================== Escalate Alert ====================
void escalateAlert() {
  if (currentAlertLevel == 0) {
    currentAlertLevel = 1;
    mqtt.publish(TOPIC_ALERTS_LEVEL, "1");
    Serial.println("[ALERT] 🔔 Alert Level 1 (LED)");
  } else if (currentAlertLevel == 1) {
    currentAlertLevel = 2;
    mqtt.publish(TOPIC_ALERTS_LEVEL, "2");
    mqtt.publish(TOPIC_ALERTS_TRIGGERED, getTimeString().c_str());
    Serial.println("[ALERT] 🔔🔔 Alert Level 2 (LED + Buzzer)");
  }
}

// ==================== Daily Refill Check ====================
void checkDailyRefillReminder() {
  int hour = getCurrentHour();

  // Reset flag after 1 PM
  if (hour >= 13) {
    dailyRefillCheckDone = false;
  }

  // Check at 12 PM
  if (hour == REFILL_CHECK_HOUR && !dailyRefillCheckDone) {
    dailyRefillCheckDone = true;

    float weight = scale.get_units(WEIGHT_READING_SAMPLES);

    if (weight < REFILL_CHECK_MIN_WEIGHT) {
      Serial.println("[ALERT] ⚠ Daily refill check FAILED - bottle low!");
      setRGB(RGB_MAGENTA);
      triggerLED(LED_ALERT_DURATION);
      triggerBuzzer(BUZZER_ALERT_DURATION);
      mqtt.publish(TOPIC_ALERTS_REFILL_CHECK, "failed");
    } else {
      Serial.println("[INFO] ✓ Daily refill check PASSED");
      mqtt.publish(TOPIC_ALERTS_REFILL_CHECK, "passed");
    }
  }
}

// ==================== Snooze Button ====================
void handleSnoozeButton() {
  if (digitalRead(SNOOZE_BUTTON_PIN) == LOW) {
    if (millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis();

      if (consecutiveSnoozeCount < MAX_CONSECUTIVE_SNOOZES) {
        snoozeUntil = millis() + SNOOZE_DURATION_MS;
        consecutiveSnoozeCount++;

        Serial.printf("[INFO] ⏸ Snooze activated (%d/%d)\n",
                      consecutiveSnoozeCount, MAX_CONSECUTIVE_SNOOZES);

        mqtt.publish(TOPIC_ALERTS_SNOOZE, "true");
        setRGB(RGB_CYAN);

        // Clear any active alert
        if (currentMode == MODE_ALERTING || currentMode == MODE_PICKED_UP ||
            currentMode == MODE_EVALUATING) {
          currentMode = MODE_MONITORING;
          currentAlertLevel = 0;
        }
      } else {
        Serial.println("[WARN] ⚠ Maximum snoozes reached!");
      }
    }
  }
}

// ==================== Helper Functions ====================
void publishTelemetry() {
  // Always try to read weight for live updates
  if (scale.wait_ready_timeout(500)) {
    float telemetryWeight = scale.get_units(5); // Fast average of 5

    // JSON telemetry for Pi5 weight command
    char telemetryJson[128];
    snprintf(telemetryJson, sizeof(telemetryJson),
             "{\"weight\":%.1f,\"delta\":%.1f,\"alert\":%d}", telemetryWeight,
             weightDelta, currentAlertLevel);

    if (mqtt.connected()) {
      mqtt.publish("hydration/telemetry", telemetryJson);
      mqtt.publish(TOPIC_WEIGHT_CURRENT, String(telemetryWeight, 1).c_str());
    }

    Serial.printf("[INFO] 📊 Live Telemetry | Weight: %.1fg | Mode: %d\n",
                  telemetryWeight, currentMode);
  } else {
    Serial.println("[WARN] ⚠ Scale not ready for telemetry");
  }
}

void setRGB(RGBColor color) {
  // Common ANODE: Invert values (LOW = ON, HIGH = OFF)
  analogWrite(RGB_RED_PIN, 255 - color.r);
  analogWrite(RGB_GREEN_PIN, 255 - color.g);
  analogWrite(RGB_BLUE_PIN, 255 - color.b);
}

void triggerLED(int duration) {
  digitalWrite(LED_NOTIFICATION_PIN, HIGH);
  delay(duration);
  digitalWrite(LED_NOTIFICATION_PIN, LOW);
}

void triggerBuzzer(int duration) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duration);
  digitalWrite(BUZZER_PIN, LOW);
}

void syncHardware() {
  // 1. Determine RGB Color
  RGBColor currentRGB = RGB_BLUE;
  if (millis() < snoozeUntil) {
    currentRGB = RGB_CYAN;
  } else if (currentMode == MODE_SLEEPING) {
    currentRGB = RGB_PURPLE;
  } else if (currentMode == MODE_AWAY || !phonePresent) {
    currentRGB = RGB_WHITE;
  } else if (currentMode == MODE_ALERTING) {
    currentRGB = RGB_ORANGE;
  } else if (currentMode == MODE_PICKED_UP) {
    currentRGB = RGB_YELLOW;
  } else if (currentMode == MODE_EVALUATING) {
    currentRGB = RGB_MAGENTA;
  } else if (currentMode == MODE_BOTTLE_MISSING) {
    // Flashing RED for missing bottle
    currentRGB = (millis() % 400 < 200) ? RGB_RED : RGB_OFF;
  }
  setRGB(currentRGB);

  // 2. Determine Notification LED (Pin 25) - BLINK PATTERN
  if (currentMode == MODE_ALERTING && currentAlertLevel >= 1) {
    // Force pin mode in case it was changed by a peripheral
    pinMode(LED_NOTIFICATION_PIN, OUTPUT);

    // Blink pattern: 500ms on, 500ms off
    bool ledState = (millis() % 1000) < 500;
    digitalWrite(LED_NOTIFICATION_PIN, ledState ? HIGH : LOW);

    // Verify and debug occasionally (removed for clean output)
  } else if (currentMode == MODE_BOTTLE_MISSING) {
    // Rapid flashing for missing bottle (200ms)
    digitalWrite(LED_NOTIFICATION_PIN, (millis() % 400 < 200) ? HIGH : LOW);
  } else {
    digitalWrite(LED_NOTIFICATION_PIN, LOW);
  }

  // 3. Determine Buzzer (Pin 26) - BEEP PATTERN
  if (currentMode == MODE_ALERTING && currentAlertLevel >= 2) {
    // Beep pattern: 200ms beep, 500ms silence (700ms total cycle)
    unsigned long pattern = millis() % 700;
    bool buzzerState = (pattern < 200);
    digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
  } else if (currentMode == MODE_BOTTLE_MISSING) {
    // 3 rapid beeps every 5 seconds
    unsigned long pattern = millis() % 5000;
    bool buzzerState = false;
    if (pattern < 600) { // 3 beeps (100ms on, 100ms off)
      buzzerState = (pattern % 200 < 100);
    }
    digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void updateRGBStatus() {
  // Deprecated - logic moved to syncHardware()
}

void handleAlertState() {
  // Uses global liveWeight updated in loop()

  // Case 1: Currently Alerting
  if (currentMode == MODE_ALERTING) {
    // Check if picked up
    if (liveWeight < PICKUP_THRESHOLD) {
      unsigned long shutdownStart = millis();
      Serial.printf("[ALERT] [PICKUP DETECTED] Weight=%.1fg Time=%lums\n",
                    liveWeight, millis());

      // INSTANT hardware shutdown - don't wait for syncHardware()
      digitalWrite(LED_NOTIFICATION_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);

      unsigned long shutdownTime = millis() - shutdownStart;
      Serial.printf("[PERF] Hardware off in %lums\n", shutdownTime);

      currentMode = MODE_PICKED_UP;
      currentAlertLevel = 0;
    }
    // Check for escalation (every 10 seconds)
    else if (millis() - alertStartTime > ALERT_WAIT_TIME &&
             currentAlertLevel < 2) {
      escalateAlert();
      alertStartTime = millis();
    }
  }
  // Case 2: Bottle is picked up
  else if (currentMode == MODE_PICKED_UP) {
    // Check if put back
    if (liveWeight > PICKUP_THRESHOLD) {
      Serial.println(
          "[INFO] ✓ Bottle replaced - waiting 2s for stabilization...");
      currentMode = MODE_EVALUATING;
      lastBottleReplacedTime = millis();
    }
  }
  // Case 3: Evaluating result
  else if (currentMode == MODE_EVALUATING) {
    // Only proceed after 2s stabilization
    if (millis() - lastBottleReplacedTime >= 2000) {
      evaluateDrinking();
    }
  }
}

void evaluateDrinking() {
  float endWeight = scale.get_units(WEIGHT_READING_SAMPLES);

  // Ghost Drink Protection: If bottle was picked up again during evaluation
  if (endWeight < PICKUP_THRESHOLD) {
    Serial.printf("[WARN] Evaluation invalid - bottle removed (%.1fg). "
                  "Returning to picked-up state.\n",
                  endWeight);
    currentMode = MODE_PICKED_UP;
    return;
  }

  float diff = endWeight - preAlertWeight;

  Serial.printf("[INFO] Evaluation | Start: %.1fg | End: %.1fg | Diff: %.1fg\n",
                preAlertWeight, endWeight, diff);

  // Drink detected
  if (diff <= -DRINK_THRESHOLD_MIN) {
    Serial.println("[INFO] ✅ User drank water!");
    setRGB(RGB_GREEN);
    delay(3000);

    todayConsumptionML += abs(diff);
    saveConsumption();

    mqtt.publish(TOPIC_CONSUMPTION_LAST, getTimeString().c_str());
    mqtt.publish(TOPIC_CONSUMPTION_INTERVAL, String(abs(diff)).c_str());
    mqtt.publish(TOPIC_CONSUMPTION_TODAY, String(todayConsumptionML).c_str());

    consecutiveSnoozeCount = 0;
    currentAlertLevel = 0;
    currentMode = MODE_MONITORING;

    // SYNC BASELINE: Tell the main loop we've already accounted for this weight
    // change
    currentWeight = endWeight;
    previousWeight = endWeight;
    lastCheckTime = millis(); // Reset periodic timer
  }
  // Refill detected
  else if (diff >= REFILL_THRESHOLD) {
    Serial.println("[INFO] 🔄 Bottle refilled!");
    setRGB(RGB_CYAN);
    delay(2000);
    currentAlertLevel = 0;
    currentMode = MODE_MONITORING;

    // SYNC BASELINE: Tell the main loop we've already accounted for this weight
    // change
    currentWeight = endWeight;
    previousWeight = endWeight;
    lastCheckTime = millis(); // Reset periodic timer
  }
  // No significant change - retry in 10 seconds
  else {
    Serial.println(
        "[ALERT] ⚠ No drink detected - retrying alert in 10 seconds");
    nextAllowedAlertTime = millis() + ALERT_RETRY_INTERVAL_MS;
    currentAlertLevel = 0;
    currentMode = MODE_MONITORING;
  }
}

String getTimeString() {
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buffer);
}

int getCurrentHour() {
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  return timeinfo->tm_hour;
}

void saveConsumption() {
  static float lastSavedML = -1.0;
  static int lastSavedDay = -1;

  // Only save if day changed or consumption changed significantly (> 0.5ml)
  if (abs(todayConsumptionML - lastSavedML) < 0.5 &&
      lastResetDay == lastSavedDay) {
    return;
  }

  prefs.begin("hydration", false);
  prefs.putFloat("today_ml", todayConsumptionML);
  prefs.putInt("last_reset", lastResetDay);
  prefs.end();

  lastSavedML = todayConsumptionML;
  lastSavedDay = lastResetDay;
  Serial.printf("[NVM] ✓ Consumption saved: %.1fml\n", todayConsumptionML);
}

void loadConsumption() {
  prefs.begin("hydration", true);
  todayConsumptionML = prefs.getFloat("today_ml", 0);
  lastResetDay = prefs.getInt("last_reset", 0);
  prefs.end();
  Serial.printf("[NVM] ✓ Loaded consumption: %.1fml (Last Reset Day: %d)\n",
                todayConsumptionML, lastResetDay);
}

void saveScaleOffset() {
  prefs.begin("hydration", false);
  long offset = scale.get_offset();
  prefs.putLong("tare_offset", offset);
  prefs.end();
  Serial.printf("💾 Saved tare offset to NVM: %ld\n", offset);
}

bool loadScaleOffset() {
  prefs.begin("hydration", true);
  if (prefs.isKey("tare_offset")) {
    long offset = prefs.getLong("tare_offset");
    scale.set_offset(offset);
    prefs.end();
    Serial.printf("📂 Loaded tare offset from NVM: %ld\n", offset);
    return true;
  }
  prefs.end();
  return false;
}
