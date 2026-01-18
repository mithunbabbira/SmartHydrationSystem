# Smart Hydration System - Complete Documentation

## Table of Contents
1. [System Overview](#system-overview)
2. [Hardware Components](#hardware-components)
3. [All System Modes](#all-system-modes)
4. [Situation Matrix](#situation-matrix)
5. [Weight Detection Logic](#weight-detection-logic)
6. [Presence Detection](#presence-detection)
7. [Alert System](#alert-system)
8. [Time-Based Behavior](#time-based-behavior)
9. [MQTT Communication](#mqtt-communication)
10. [Edge Cases & Safety](#edge-cases--safety)

---

## System Overview

The Smart Hydration Reminder System is an ESP32-based intelligent water bottle monitor that:
- Tracks water consumption using a 5kg load cell (HX711)
- Detects user presence via Bluetooth Classic (Samsung S25 Ultra)
- Sends escalating reminders when you haven't drunk water
- Publishes telemetry to Raspberry Pi 5 via MQTT
- Provides visual feedback through RGB LED
- Integrates with web dashboard for monitoring

**Key Intelligence:**
- Knows when you're sleeping (11 PM - 10 AM)
- Detects when you're away from home (Bluetooth)
- Stops alerts after daily goal achieved (2000ml)
- Prevents false detection of "drinking"
- Allows snoozing (with limits)

---

## Hardware Components

### Sensors
- **HX711 Load Cell Amplifier**
  - DOUT Pin: GPIO 32
  - SCK Pin: GPIO 33
  - Calibration Factor: 350.3
  - Precision: ±0.1g

### Outputs
- **White LED (Notification)**
  - Pin: GPIO 25
  - Alert indicator (blinking pattern)

- **RGB LED (Status)**
  - Red Pin: GPIO 27
  - Green Pin: GPIO 14
  - Blue Pin: GPIO 12
  - Common ANODE (inverted logic)

- **Buzzer**
  - Pin: GPIO 26
  - Beep pattern during alerts

### Inputs
- **Snooze Button**
  - Pin: GPIO 13
  - Pull-up resistor (LOW when pressed)
  - Debounce: 200ms

### Connectivity
- **WiFi**: 2.4GHz (802.11 b/g/n)
- **Bluetooth Classic**: ESP-IDF GAP API
- **MQTT**: Client to Raspberry Pi broker

---

## All System Modes

### MODE_INITIALIZING (0)
**When:** System boot only  
**Duration:** ~10-20 seconds  
**What Happens:**
1. Load saved tare offset from NVM
2. Read initial weight baseline
3. Connect to WiFi
4. Sync time via NTP
5. Connect to MQTT broker
6. Check if new day (reset consumption)
7. Transition to MODE_MONITORING

**RGB:** 🔵 Blue  
**Checks:** None  
**Exits To:** MODE_MONITORING

---

### MODE_MONITORING (1)
**When:** Normal operation (10 AM - 11 PM, user home, bottle present)  
**What Happens:**
```
Every 30 minutes:
  1. Read current weight (average of 10 samples)
  2. Calculate delta from previous weight
  
  IF delta <= -30g:
    → User drank water
    → Add |delta| to today's consumption
    → Green LED for 3 seconds
    → Publish consumption to MQTT
    → Reset snooze counter
    
  ELSE IF delta >= 100g:
    → Bottle refilled
    → Cyan LED for 2 seconds
    → Do NOT add to consumption
    
  ELSE IF STOP_ALERTS_AFTER_GOAL && todayML >= 2000:
    → Goal achieved!
    → Skip alert
    → Log to serial
    
  ELSE:
    → No drinking detected
    → Enter MODE_ALERTING
    → Start alert escalation
```

**RGB:** 🔵 Blue  
**LED:** OFF  
**Buzzer:** OFF  
**Checks:** Every 30 minutes  
**Exits To:** 
- MODE_ALERTING (no drinking)
- MODE_SLEEPING (time reached 11 PM)
- MODE_AWAY (phone not detected for 5 min)
- MODE_BOTTLE_MISSING (bottle removed >3 min)

---

### MODE_SLEEPING (2)
**When:** 11 PM - 10 AM (SLEEP_START_HOUR to SLEEP_END_HOUR)  
**What Happens:**
- System continues running
- Scale monitoring active
- **NO** drink checks
- **NO** alerts
- **NO** reminders
- MQTT telemetry still published every 30s

**RGB:** 🟣 Purple  
**LED:** OFF  
**Buzzer:** OFF  
**Checks:** None (passive monitoring only)  
**Exits To:** MODE_MONITORING (when 10 AM reached)

**Example:**
```
10:58 PM: MODE_MONITORING → User drank
11:00 PM: MODE_SLEEPING (automatic transition)
11:30 PM: (skip check - sleeping)
12:00 AM: Midnight reset (consumption → 0ml)
6:00 AM:  (still sleeping - no checks)
10:00 AM: MODE_MONITORING (wake up!)
```

---

### MODE_AWAY (3)
**When:** User's phone not detected via Bluetooth  
**How Detected:**
```
Every 10 seconds:
  Bluetooth ping to MAC 48:EF:1C:49:6A:E7
  
  IF phone responds:
    presenceFailCount = 0
    phonePresent = true
  
  ELSE:
    presenceFailCount++
    
    IF presenceFailCount >= 30:  // 5 minutes
      phonePresent = false
      → Enter MODE_AWAY
      → MQTT: "hydration/status/bluetooth" = "disconnected"
```

**What Happens:**
- System stays active
- Scale monitoring continues
- **NO** drink checks (you're not home!)
- **NO** alerts
- Telemetry still published

**RGB:** ⚪ White  
**LED:** OFF  
**Buzzer:** OFF  
**Checks:** None  
**Exits To:** MODE_MONITORING (when phone detected)

**Example:**
```
2:00 PM: Phone lost (count: 1/30)
2:00 PM: Phone lost (count: 2/30)
...
2:05 PM: Phone lost (count: 30/30) → MODE_AWAY
2:05 PM: RGB changes to White
2:30 PM: (skip check - away)
5:00 PM: Phone detected! → MODE_MONITORING
5:00 PM: RGB changes to Blue
```

---

### MODE_SNOOZED (4)
**When:** Snooze button pressed OR MQTT snooze command sent  
**Duration:** 5 minutes (SNOOZE_DURATION_MS)  
**Limit:** Max 3 consecutive snoozes  

**What Happens:**
```
IF consecutiveSnoozeCount < 3:
  snoozeUntil = now + 5 minutes
  consecutiveSnoozeCount++
  → Enter MODE_SNOOZED
  → Cyan LED
  → Clear any active alert
  → MQTT: "hydration/alerts/snooze_active" = "true"
  
ELSE:
  → "Max snoozes reached!"
  → Button ignored
  → Continue current mode
```

**RGB:** 🔵 Cyan  
**LED:** OFF  
**Buzzer:** OFF  
**Checks:** Skipped during snooze period  
**Exits To:** Previous mode (when 5 min expires)

**Example:**
```
10:30 AM: Alert triggered (didn't drink)
10:30 AM: Press snooze → MODE_SNOOZED (1/3)
10:35 AM: Snooze expires → MODE_MONITORING

11:00 AM: Alert triggered again
11:00 AM: Press snooze → MODE_SNOOZED (2/3)
11:05 AM: Snooze expires → MODE_MONITORING

11:30 AM: Alert triggered again
11:30 AM: Press snooze → MODE_SNOOZED (3/3)
11:35 AM: Snooze expires → MODE_MONITORING

12:00 PM: Alert triggered again
12:00 PM: Press snooze → "Max snoozes!" (ignored)
12:00 PM: You MUST drink to reset counter
```

---

### MODE_ALERTING (5)
**When:** 30 minutes passed, no drinking detected  
**What Happens:**

**Phase 1 (Alert Level 1):**
```
0 seconds:
  - Enter MODE_ALERTING
  - alertLevel = 1
  - RGB → Orange
  - White LED: Blinking (500ms ON, 500ms OFF)
  - MQTT: "hydration/alerts/level" = "1"
  - Record preAlertWeight (for later comparison)
```

**Phase 2 (10 seconds later):**
```
10 seconds:
  IF still alerting:
    - alertLevel = 2
    - LED: Continue blinking
    - Buzzer: Beeping (200ms beep, 500ms silence)
    - MQTT: "hydration/alerts/level" = "2"
    - MQTT: "hydration/alerts/triggered" = "<timestamp>"
```

**Exits When:**
- Bottle picked up (weight < 50g) → MODE_PICKED_UP
- Snooze button pressed → MODE_SNOOZED

**RGB:** 🟠 Orange  
**LED:** Blinking (500ms pattern)  
**Buzzer:** Beeping (200ms beep, 500ms gap) - Level 2 only  

**Hardware Timing:**
```
Loop runs every ~10ms
Blink pattern: checked every loop via millis()
  - LED:    millis() % 1000 < 500 ? ON : OFF
  - Buzzer: millis() % 700 < 200 ? ON : OFF
```

---

### MODE_PICKED_UP (6)
**When:** During MODE_ALERTING, weight drops below 50g  
**What Happens:**
```
Immediate actions (within 10ms):
  1. digitalWrite(LED, LOW)      // Instant OFF
  2. digitalWrite(BUZZER, LOW)   // Instant OFF
  3. alertLevel = 0
  4. RGB → Yellow
  5. Wait for bottle to return
```

**Waiting State:**
- Continuously monitors weight
- Waiting for weight to exceed 50g (bottle replaced)

**RGB:** 🟡 Yellow  
**LED:** OFF (instantly)  
**Buzzer:** OFF (instantly)  
**Checks:** Continuous weight monitoring  
**Exits To:** MODE_EVALUATING (when bottle replaced)

**Why Instant OFF?**
```cpp
// INSTANT hardware shutdown - don't wait for syncHardware()
digitalWrite(LED_NOTIFICATION_PIN, LOW);
digitalWrite(BUZZER_PIN, LOW);
```
This ensures the moment you pick up the bottle, the alert stops. No delay!

---

### MODE_EVALUATING (7)
**When:** Bottle replaced after being picked up during alert  
**What Happens:**

**Step 1: Stabilization (2 seconds)**
```
When bottle replaced (weight > 50g):
  1. Set MODE_EVALUATING
  2. RGB → Magenta
  3. lastBottleReplacedTime = now
  4. Wait 2 seconds for weight to stabilize
```

**Step 2: Evaluation**
```
After 2 seconds:
  Read endWeight (average of 10 samples)
  
  1. Ghost Drink Protection:
     IF endWeight < 50g:
       → Bottle removed again during evaluation
       → Return to MODE_PICKED_UP
       → "Invalid - bottle removed"
  
  2. Calculate difference:
     diff = endWeight - preAlertWeight
  
  3. Decision:
     
     IF diff <= -30g:
       → "User drank water!" ✅
       → Green LED for 3 seconds
       → Add |diff| to consumption
       → MQTT publish consumption
       → consecutiveSnoozeCount = 0
       → MODE_MONITORING
     
     ELSE IF diff >= 100g:
       → "Bottle refilled!" 🔄
       → Cyan LED for 2 seconds
       → Do NOT add to consumption
       → MODE_MONITORING
     
     ELSE:
       → "No significant change"
       → Just checked the bottle
       → No credit, no penalty
       → MODE_MONITORING
       → Next retry in 10 minutes
```

**RGB:** 🟣 Magenta  
**LED:** OFF  
**Buzzer:** OFF  
**Duration:** 2 seconds + evaluation time  
**Exits To:** MODE_MONITORING (always)

**Example Scenarios:**

**Scenario A: Actual Drinking**
```
10:00 AM: Alert (weight: 650g)
10:01 AM: Pick up (preAlertWeight = 650g)
10:02 AM: Put back (weight: 605g)
10:02 AM: Wait 2 seconds...
10:02 AM: Evaluate: 605g - 650g = -45g
10:02 AM: "Drank 45ml!" ✅
10:02 AM: Today: 45ml / 2000ml
```

**Scenario B: Just Checking**
```
10:00 AM: Alert (weight: 650g)
10:01 AM: Pick up (preAlertWeight = 650g)
10:01 AM: Put back (weight: 648g)
10:01 AM: Wait 2 seconds...
10:01 AM: Evaluate: 648g - 650g = -2g
10:01 AM: "No significant change" (noise)
10:01 AM: Back to monitoring
```

**Scenario C: Refilled**
```
10:00 AM: Alert (weight: 200g, bottle low)
10:01 AM: Pick up (preAlertWeight = 200g)
10:02 AM: Refill + put back (weight: 820g)
10:02 AM: Wait 2 seconds...
10:02 AM: Evaluate: 820g - 200g = +620g
10:02 AM: "Refilled!" 🔄
10:02 AM: No consumption credit
```

---

### MODE_BOTTLE_MISSING (8)
**When:** Bottle off scale (weight < 50g) for more than 3 minutes  
**What Happens:**

**Detection Logic:**
```
Continuous monitoring in main loop:
  
  IF weight < 50g:
    IF (now - lastBottlePresentTime) > 180000ms:
      IF currentMode != MODE_BOTTLE_MISSING:
        → Enter MODE_BOTTLE_MISSING
        → MQTT: "hydration/alerts/bottle_missing" = "active"
        → Serial: "CRITICAL: Bottle missing for over 3 min!"
```

**Visual/Audio Alerts:**
```
RGB LED Pattern:
  Rapid flashing Red
  millis() % 400 < 200 ? RED : OFF
  (200ms ON, 200ms OFF)

White LED Pattern:
  Rapid flashing
  millis() % 400 < 200 ? ON : OFF

Buzzer Pattern:
  3 rapid beeps every 5 seconds
  Pattern:
    0-100ms:   BEEP
    100-200ms: silent
    200-300ms: BEEP
    300-400ms: silent
    400-500ms: BEEP
    500-5000ms: silent
    (repeat)
```

**RGB:** 🔴 Red (flashing)  
**LED:** Rapid flashing  
**Buzzer:** 3-beep pattern every 5s  
**Exits To:** MODE_MONITORING (when bottle replaced)

**Example:**
```
2:00 PM: Remove bottle (weight: 5g)
2:00 PM: lastBottlePresentTime recorded
2:01 PM: Still missing (1 min)
2:02 PM: Still missing (2 min)
2:03 PM: Still missing (3 min) → MODE_BOTTLE_MISSING
2:03 PM: Red LED flashing, 3-beep pattern
2:03 PM: MQTT alert sent
2:05 PM: Put bottle back (weight: 650g)
2:05 PM: MODE_MONITORING restored
2:05 PM: MQTT: "bottle_missing" = "cleared"
```

---

## Situation Matrix

### All Possible Combinations

| Time | Presence | Bottle | Snoozed | Goal | Result Mode | Alerts? |
|------|----------|--------|---------|------|-------------|---------|
| 10 AM | Home | Present | No | <2000ml | MONITORING | ✅ Yes |
| 10 AM | Home | Present | Yes | <2000ml | SNOOZED | ❌ No |
| 10 AM | Home | Present | No | >=2000ml | MONITORING | ❌ No (goal met) |
| 10 AM | Home | Missing | No | Any | BOTTLE_MISSING | ⚠️ Missing alert |
| 10 AM | Away | Present | No | <2000ml | AWAY | ❌ No |
| 10 AM | Away | Missing | No | Any | AWAY | ❌ No |
| 11 PM | Home | Present | No | Any | SLEEPING | ❌ No |
| 11 PM | Away | Present | No | Any | SLEEPING | ❌ No |
| 11 PM | Home | Missing | No | Any | SLEEPING | ❌ No |
| Alerting | Home | Present | No | <2000ml | ALERTING | ✅ Yes (LED+Buzzer) |
| Alerting | Home | Picked Up | No | <2000ml | PICKED_UP | ❌ Instant OFF |
| Evaluating | Home | Present | No | <2000ml | EVALUATING | ❌ No (checking) |

---

## Weight Detection Logic

### Threshold Values (config.h)

```c
DRINK_THRESHOLD_MIN     = 30g    // Minimum to count as drinking
REFILL_THRESHOLD        = 100g   // Minimum to count as refill
PICKUP_THRESHOLD        = 50g    // Below this = bottle removed
REFILL_CHECK_MIN_WEIGHT = 1500g  // Daily noon check
DAILY_GOAL_ML           = 2000ml // Daily target
```

### Detection Algorithm

```cpp
float currentWeight = scale.get_units(10);  // Average of 10 samples
float previousWeight = <stored_value>;
float delta = currentWeight - previousWeight;

// Sanity check: Impossible jump detection
if (abs(delta) > 1000 && mode != MODE_PICKED_UP) {
  // Likely sensor glitch
  Serial.println("Impossible weight jump! Syncing baseline only.");
  previousWeight = currentWeight;
  delta = 0;
  return;  // Don't process as drink/refill
}

// Actual detection
if (delta <= -DRINK_THRESHOLD_MIN) {
  // Drinking detected
  float ml = abs(delta);
  todayConsumptionML += ml;
  
  // Publish to MQTT
  mqtt.publish("hydration/consumption/interval_ml", String(ml));
  mqtt.publish("hydration/consumption/today_ml", String(todayConsumptionML));
  mqtt.publish("hydration/consumption/last_drink", getTimeString());
  
  // Save to NVM
  saveConsumption();
  
  // Visual feedback
  setRGB(RGB_GREEN);
  delay(3000);
  
  // Reset snooze counter
  consecutiveSnoozeCount = 0;
}
else if (delta >= REFILL_THRESHOLD) {
  // Refill detected
  setRGB(RGB_CYAN);
  delay(2000);
  // NO consumption credit
}
else {
  // No significant change → Start alert
  if (!STOP_ALERTS_AFTER_GOAL || todayConsumptionML < DAILY_GOAL_ML) {
    currentMode = MODE_ALERTING;
    preAlertWeight = currentWeight;
    alertStartTime = millis();
    escalateAlert();  // Level 1
  }
}
```

### Weight Reading Process

**Single Sample:**
```cpp
float weight = scale.get_units(1);  // One reading
```

**Averaged Sample (more stable):**
```cpp
float weight = scale.get_units(10);  // Average of 10 readings
// Reduces noise, more reliable
// Used for: initial baseline, drink detection
```

**High-precision Sample:**
```cpp
float weight = scale.get_units(20);  // Average of 20 readings
// Highest precision
// Used for: boot baseline
```

**Live Weight (loop):**
```cpp
// Updated once per loop iteration
if (scale.wait_ready_timeout(1)) {
  liveWeight = scale.get_units(1);  // Fast, single sample
}
// Used for: real-time bottle presence detection
```

---

## Presence Detection

### Bluetooth Classic Implementation

**Technology:** ESP-IDF Bluetooth GAP API (not BLE)

**Target Device:**
- Type: Samsung S25 Ultra
- MAC Address: `48:EF:1C:49:6A:E7`
- Detection Method: Remote Name Request (paging)

**How It Works:**

```cpp
// Global state
esp_bd_addr_t phone_bt_addr = {0x48, 0xEF, 0x1C, 0x49, 0x6A, 0xE7};
volatile bool bt_scan_finished = false;
volatile bool bt_device_found = false;

// GAP Callback (asynchronous)
void bt_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  if (event == ESP_BT_GAP_READ_REMOTE_NAME_EVT) {
    bt_scan_finished = true;
    if (param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS) {
      bt_device_found = true;
    } else {
      bt_device_found = false;
    }
  }
}

// Check function (called every 10 seconds)
void checkPhonePresence() {
  bt_scan_finished = false;
  bt_device_found = false;
  
  // Start remote name request (Bluetooth Classic "ping")
  esp_err_t ret = esp_bt_gap_read_remote_name(phone_bt_addr);
  
  if (ret != ESP_OK) {
    Serial.println("[PRESENCE] BT scan failed to start");
    presenceFailCount++;
  } else {
    // Wait for callback (max 8 seconds)
    unsigned long start = millis();
    while (!bt_scan_finished && (millis() - start < 8000)) {
      delay(100);
    }
    
    if (bt_device_found) {
      // Phone detected!
      if (!phonePresent) {
        phonePresent = true;
        presenceFailCount = 0;
        mqtt.publish("hydration/status/bluetooth", "connected");
        Serial.println("[PRESENCE] ✓ Samsung S25 Ultra detected");
      }
      presenceFailCount = 0;
    } else {
      // Phone not found
      presenceFailCount++;
      Serial.printf("[PRESENCE] Phone not found (fail count: %d/30)\n", presenceFailCount);
      
      if (presenceFailCount >= 30 && phonePresent) {
        // 5 minutes of failures → mark as away
        phonePresent = false;
        mqtt.publish("hydration/status/bluetooth", "disconnected");
        Serial.println("[PRESENCE] ✗ User marked as AWAY");
      }
    }
  }
}
```

**Why Bluetooth Classic Instead of BLE?**

| Feature | BLE (Tried First) | Bluetooth Classic (Current) |
|---------|-------------------|----------------------------|
| Phone advertises when screen off | ❌ No (iPhone/Samsung) | ✅ Yes (responds to paging) |
| MAC address | 🔀 Random | ✅ Fixed |
| Reliability | ⚠️ Spotty | ✅ Consistent |
| Detection method | Passive scan | Active query (hcitool) |
| Works with locked phone | ❌ Often fails | ✅ Works |

**Timing:**
- Check every: 10 seconds
- Timeout for single check: 8 seconds
- Fail threshold: 30 checks (5 minutes)
- Recovery: Immediate (single successful detection)

**Example Timeline:**
```
12:00:00 PM: Check #1 → Found ✅ (presenceFailCount = 0)
12:00:10 PM: Check #2 → Found ✅ (presenceFailCount = 0)
12:00:20 PM: Check #3 → NOT found ❌ (presenceFailCount = 1)
12:00:30 PM: Check #4 → NOT found ❌ (presenceFailCount = 2)
...
12:05:00 PM: Check #30 → NOT found ❌ (presenceFailCount = 30)
12:05:00 PM: → MODE_AWAY, MQTT: "disconnected"
12:05:10 PM: Check #31 → Found ✅ (presenceFailCount = 0)
12:05:10 PM: → MODE_MONITORING, MQTT: "connected"
```

---

## Alert System

### Escalation Timeline

```
T = 0:00
  ├─ Weight check (no drinking)
  ├─ Enter MODE_ALERTING
  ├─ Set alertLevel = 1
  ├─ RGB → Orange
  ├─ LED starts blinking
  └─ MQTT: "hydration/alerts/level" = "1"

T = 0:10 (10 seconds later)
  ├─ Check: Still alerting?
  ├─ YES → Escalate
  ├─ Set alertLevel = 2
  ├─ LED continues blinking
  ├─ Buzzer starts beeping
  ├─ MQTT: "hydration/alerts/level" = "2"
  └─ MQTT: "hydration/alerts/triggered" = "<timestamp>"

T = ongoing
  └─ Wait for one of:
      - Bottle picked up → MODE_PICKED_UP
      - Snooze pressed → MODE_SNOOZED
      - Never exits automatically!
```

### Hardware Patterns (syncHardware function)

**Called:** Every loop iteration (~10ms)

**White LED (Pin 25):**
```cpp
if (currentMode == MODE_ALERTING && alertLevel >= 1) {
  pinMode(LED_NOTIFICATION_PIN, OUTPUT);
  bool ledState = (millis() % 1000) < 500;  // 500ms ON, 500ms OFF
  digitalWrite(LED_NOTIFICATION_PIN, ledState ? HIGH : LOW);
}
else if (currentMode == MODE_BOTTLE_MISSING) {
  // Rapid flash
  bool ledState = (millis() % 400) < 200;  // 200ms ON, 200ms OFF
  digitalWrite(LED_NOTIFICATION_PIN, ledState ? HIGH : LOW);
}
else {
  digitalWrite(LED_NOTIFICATION_PIN, LOW);  // OFF
}
```

**Buzzer (Pin 26):**
```cpp
if (currentMode == MODE_ALERTING && alertLevel >= 2) {
  // Beep pattern: 200ms beep, 500ms silence (700ms total)
  unsigned long pattern = millis() % 700;
  bool buzzerState = (pattern < 200);
  digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
}
else if (currentMode == MODE_BOTTLE_MISSING) {
  // 3 rapid beeps every 5 seconds
  unsigned long pattern = millis() % 5000;
  bool buzzerState = false;
  if (pattern < 600) {  // First 600ms
    buzzerState = (pattern % 200 < 100);  // Beep pattern
  }
  digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
}
else {
  digitalWrite(BUZZER_PIN, LOW);  // OFF
}
```

**RGB LED (Common Anode - Inverted):**
```cpp
void setRGB(RGBColor color) {
  // Common ANODE: LOW = ON, HIGH = OFF
  analogWrite(RGB_RED_PIN, 255 - color.r);
  analogWrite(RGB_GREEN_PIN, 255 - color.g);
  analogWrite(RGB_BLUE_PIN, 255 - color.b);
}

// Mode-based colors (in syncHardware)
if (millis() < snoozeUntil) {
  setRGB(RGB_CYAN);  // Snoozed
}
else if (currentMode == MODE_SLEEPING) {
  setRGB(RGB_PURPLE);  // Sleeping
}
else if (currentMode == MODE_AWAY || !phonePresent) {
  setRGB(RGB_WHITE);  // Away
}
else if (currentMode == MODE_ALERTING) {
  setRGB(RGB_ORANGE);  // Alerting
}
else if (currentMode == MODE_PICKED_UP) {
  setRGB(RGB_YELLOW);  // Picked up
}
else if (currentMode == MODE_EVALUATING) {
  setRGB(RGB_MAGENTA);  // Evaluating
}
else if (currentMode == MODE_BOTTLE_MISSING) {
  // Flashing red
  RGBColor currentRGB = (millis() % 400 < 200) ? RGB_RED : RGB_OFF;
  setRGB(currentRGB);
}
else {
  setRGB(RGB_BLUE);  // Normal monitoring
}
```

---

## Time-Based Behavior

### Sleep Hours (config.h)
```c
#define SLEEP_START_HOUR 23   // 11 PM
#define SLEEP_END_HOUR 10     // 10 AM
```

### Check Logic

```cpp
int getCurrentHour() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return -1;  // Time not available
  }
  return timeinfo.tm_hour;  // 0-23
}

// In main loop
int hour = getCurrentHour();
if (hour >= SLEEP_START_HOUR || hour < SLEEP_END_HOUR) {
  currentMode = MODE_SLEEPING;
}
else if (currentMode == MODE_SLEEPING) {
  currentMode = MODE_MONITORING;  // Wake up!
}
```

**Automatic Transitions:**
```
9:59 AM: MODE_SLEEPING
10:00 AM: MODE_MONITORING (automatic)

10:58 PM: MODE_MONITORING
11:00 PM: MODE_SLEEPING (automatic)
```

### Daily Refill Check

**Time:** 12:00 PM (REFILL_CHECK_HOUR = 12)

```cpp
void checkDailyRefillReminder() {
  int hour = getCurrentHour();
  
  // Reset flag after 1 PM
  if (hour >= 13) {
    dailyRefillCheckDone = false;
  }
  
  // Check at 12 PM (once)
  if (hour == 12 && !dailyRefillCheckDone) {
    dailyRefillCheckDone = true;
    
    float weight = scale.get_units(WEIGHT_READING_SAMPLES);
    
    if (weight < REFILL_CHECK_MIN_WEIGHT) {  // < 1500g
      Serial.println("[ALERT] Daily refill check FAILED - bottle low!");
      setRGB(RGB_MAGENTA);
      triggerLED(LED_ALERT_DURATION);   // 10 seconds
      triggerBuzzer(BUZZER_ALERT_DURATION);  // 10 seconds
      mqtt.publish("hydration/alerts/daily_refill_check", "failed");
    }
    else {
      Serial.println("[INFO] Daily refill check PASSED");
      mqtt.publish("hydration/alerts/daily_refill_check", "passed");
    }
  }
}
```

**Example:**
```
11:59 AM: dailyRefillCheckDone = false
12:00 PM: Check weight → 1450g
12:00 PM: < 1500g → FAILED
12:00 PM: Magenta LED + Buzzer for 10 seconds
12:00 PM: MQTT: "failed"
12:01 PM: dailyRefillCheckDone = true (won't check again)
1:00 PM: dailyRefillCheckDone = false (reset for tomorrow)
```

### Midnight Reset

```cpp
// Called every 60 seconds in main loop
struct tm timeinfo;
if (getLocalTime(&timeinfo)) {
  if (timeinfo.tm_mday != lastResetDay) {
    Serial.println("[INFO] 🌅 Midnight reset triggered!");
    todayConsumptionML = 0;
    lastResetDay = timeinfo.tm_mday;
    saveConsumption();
    
    if (mqtt.connected()) {
      mqtt.publish("hydration/consumption/today", "0");
      mqtt.publish("hydration/status/message", "Daily consumption reset at midnight");
    }
  }
}
```

**Example:**
```
Jan 18, 11:59 PM: Today = 1850ml, lastResetDay = 18
Jan 19, 12:00 AM: tm_mday = 19 (different!)
Jan 19, 12:00 AM: Today = 0ml, lastResetDay = 19
Jan 19, 12:00 AM: MQTT publish "0"
```

---

## MQTT Communication

### Broker Connection

**Target:** Raspberry Pi 5  
**Hostname:** raspberrypi.local (resolved via mDNS)  
**Port:** 1883  
**Auth:** Anonymous (no username/password)  
**Client ID:** "ESP32_Hydration"

### Published Topics

| Topic | Payload | Frequency | Description |
|-------|---------|-----------|-------------|
| `hydration/status/online` | "true"/"false" | On connect/disconnect | ESP32 online status |
| `hydration/status/bluetooth` | "connected"/"disconnected" | On change | Phone presence |
| `hydration/weight/current` | "625.1" (float) | Every 30s | Current bottle weight |
| `hydration/weight/delta` | "-40.5" (float) | After check | Last weight change |
| `hydration/consumption/today_ml` | "1234.5" (float) | After drinking | Total today |
| `hydration/consumption/interval_ml` | "45.0" (float) | After drinking | Amount drunk |
| `hydration/consumption/last_drink` | "2026-01-19 14:32:15" | After drinking | Timestamp |
| `hydration/alerts/level` | "0"/"1"/"2" | On change | Alert escalation level |
| `hydration/alerts/triggered` | Timestamp | Level 2 reached | When buzzer started |
| `hydration/alerts/snooze_active` | "true"/"false" | On change | Snooze status |
| `hydration/alerts/bottle_missing` | "active"/"cleared" | On change | Bottle missing alert |
| `hydration/alerts/daily_refill_check` | "passed"/"failed" | Daily at noon | Refill check result |
| `hydration/telemetry` | JSON | Every 30s | Batched telemetry |

### Telemetry JSON Format

```json
{
  "weight": 625.1,
  "delta": 0.0,
  "alert": 0
}
```

### Subscribed Topics (Commands)

| Topic | Payload | Action |
|-------|---------|--------|
| `hydration/commands/tare_scale` | "execute" | Re-calibrate scale to zero |
| `hydration/commands/trigger_led` | "on" | Test LED for 2 seconds |
| `hydration/commands/trigger_buzzer` | "on" | Test buzzer for 1 second |
| `hydration/commands/trigger_rgb` | "red"/"green"/"blue"/"off" | Set RGB color for 2s |
| `hydration/commands/snooze` | "15" (minutes) | Activate snooze |
| `hydration/commands/reboot` | "execute" | Restart ESP32 |
| `hydration/commands/reset_today` | "execute" | Reset daily consumption to 0 |

### Command Examples

**Remote Tare:**
```bash
mosquitto_pub -h raspberrypi.local -t 'hydration/commands/tare_scale' -m 'execute'
```
ESP32 action:
```cpp
scale.tare();                    // Re-zero
saveScaleOffset();               // Save to NVM
mqtt.publish("hydration/status/message", "Scale tared and saved to NVM");
```

**Remote Snooze:**
```bash
mosquitto_pub -h raspberrypi.local -t 'hydration/commands/snooze' -m '15'
```
ESP32 action:
```cpp
snoozeUntil = millis() + (15 * 60 * 1000);  // 15 minutes
mqtt.publish("hydration/alerts/snooze_active", "true");
currentMode = MODE_MONITORING;
currentAlertLevel = 0;
```

---

## Edge Cases & Safety

### 1. Boot with Bottle On Scale

**Problem:** If you boot with the bottle already on the scale, the system sets that weight as the baseline.

**Detection:**
```cpp
// In setup(), after loading tare offset
float initialWeight = scale.get_units(20);

if (initialWeight > PICKUP_THRESHOLD) {  // > 50g
  Serial.printf("[SETUP] ⚠ BOTTLE DETECTED ON BOOT! (%.1fg)\n", initialWeight);
  Serial.println("[SETUP] This weight will become the BASELINE.");
  Serial.println("[SETUP] To prevent this, remove bottle before booting OR use remote tare.");
}

// Sets baseline regardless (current behavior)
currentWeight = initialWeight;
previousWeight = initialWeight;
```

**Result:** Later when you remove the bottle, delta will be negative (appears as "drinking")

**Solutions:**
1. **Best:** Remove bottle before powering on
2. **After boot:** Use remote tare command when scale is empty
3. **Code fix (future):** Detect bottle on boot and wait for removal before setting baseline

### 2. Sensor Glitch / Impossible Jumps

**Problem:** Occasional sensor glitches cause wild readings

**Protection:**
```cpp
if (abs(weightDelta) > 1000 && currentMode != MODE_PICKED_UP) {
  Serial.println("[WARN] Impossible weight jump detected! Syncing baseline only.");
  previousWeight = currentWeight;
  weightDelta = 0;
  // Don't treat as drink or refill!
  return;
}
```

**Example:**
```
10:00 AM: Weight = 650g
10:30 AM: Weight = 1802g (sensor glitch +1152g)
10:30 AM: Detected as impossible
10:30 AM: Baseline sync to 650g, delta → 0
10:30 AM: No false "refill" credit
```

### 3. Ghost Drink Prevention

**Problem:** User picks up bottle during alert but doesn't drink

**Protection in MODE_EVALUATING:**
```cpp
void evaluateDrinking() {
  float endWeight = scale.get_units(WEIGHT_READING_SAMPLES);
  
  // Check if bottle was removed again during evaluation
  if (endWeight < PICKUP_THRESHOLD) {
    Serial.println("[WARN] Evaluation invalid - bottle removed.");
    currentMode = MODE_PICKED_UP;
    return;  // No credit!
  }
  
  float diff = endWeight - preAlertWeight;
  
  if (diff <= -DRINK_THRESHOLD_MIN) {
    // Legitimate drink
  }
  else {
    // Just checking or putting back without drinking
    // No credit, back to monitoring
  }
}
```

**Example:**
```
10:00 AM: Alert (650g)
10:01 AM: Pick up
10:01 AM: Immediately put back (650g, no drinking)
10:01 AM: Wait 2 seconds...
10:01 AM: diff = 650g - 650g = 0g
10:01 AM: "No significant change" → No credit ✅
```

### 4. Negative Weight on Boot

**Problem:** Saved tare offset is old, scale shows negative weight

**Detection:**
```cpp
if (initialWeight < -50) {
  Serial.printf("[SETUP] ⚠ NEGATIVE WEIGHT DETECTED! (%.1fg)\n", initialWeight);
  Serial.println("[SETUP] Saved tare offset might be outdated.");
  Serial.println("[SETUP] TIP: Use 'hydration/commands/tare_scale' to re-calibrate when empty.");
}
// System continues with negative baseline (won't auto-tare)
```

**Why not auto-tare?**
- Bottle might be on scale (creating negative reading)
- Better to warn user than make wrong assumption

### 5. WiFi/MQTT Disconnection

**Behavior:**
- System continues operating in standalone mode
- Scale monitoring continues
- Alerts continue
- Consumption tracked locally
- Telemetry queued (not sent)

**Recovery:**
```cpp
// Main loop checks every 60 seconds
if (!mqtt.connected()) {
  if (millis() - lastMqttAttempt >= 60000) {
    reconnectMQTT();  // Try once
    lastMqttAttempt = millis();
  }
}
// Non-blocking! System doesn't hang
```

**Example:**
```
2:00 PM: WiFi disconnected
2:00 PM: System continues monitoring
2:01 PM: Check weight → User drank (tracked locally)
2:02 PM: WiFi reconnected
2:02 PM: MQTT reconnected
2:02 PM: Publishes current state
```

### 6. Button Debouncing

**Problem:** Mechanical button bounce causes multiple detections

**Protection:**
```cpp
const unsigned long debounceDelay = 200;  // ms

void handleSnoozeButton() {
  if (digitalRead(SNOOZE_BUTTON_PIN) == LOW) {
    if (millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis();
      // Process button press
    }
  }
}
```

### 7. NVM Persistence

**What's Saved:**
- Tare offset (scale calibration)
- Today's consumption (ml)
- Last reset day

**When Saved:**
- Tare: After every tare operation
- Consumption: After every drink
- Day: At midnight reset

**Recovery After Power Loss:**
```cpp
void setup() {
  // Load from NVM
  loadScaleOffset();    // Restore tare
  loadConsumption();    // Restore today's total
  
  // Check if new day
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    if (timeinfo.tm_mday != lastResetDay) {
      // New day! Reset
      todayConsumptionML = 0;
    }
  }
}
```

---

## Summary of All Conditions

### When Alerts Happen
✅ **YES** - Alert will trigger:
- 10 AM - 11 PM (active hours)
- Phone detected (home)
- Bottle present (weight > 50g)
- Not snoozed
- Goal not met (<2000ml)
- No drinking detected

❌ **NO** - Alert will NOT trigger:
- 11 PM - 10 AM (sleeping)
- Phone not detected (away)
- Bottle missing
- Snoozed (5 min grace)
- Goal met (>=2000ml)
- Just drank water

### Mode Priority
```
1. MODE_SLEEPING      (highest - overrides everything)
2. MODE_BOTTLE_MISSING (critical alert)
3. MODE_AWAY          (user not home)
4. MODE_SNOOZED       (user requested pause)
5. MODE_ALERTING      (active reminder)
6. MODE_PICKED_UP     (bottle removed during alert)
7. MODE_EVALUATING    (checking if user drank)
8. MODE_MONITORING    (default state)
```

### RGB Color Summary
- 🔵 Blue: Normal monitoring
- 🟣 Purple: Sleeping (11 PM - 10 AM)
- ⚪ White: Away from home
- 🟠 Orange: Alert active (didn't drink)
- 🟡 Yellow: Bottle picked up during alert
- 🟣 Magenta: Evaluating if you drank
- 🔴 Red (flashing): Bottle missing >3 min
- 🔵 Cyan: Snoozed or refilled
- 🟢 Green: Success! You drank (3 sec)

---

## Quick Reference Card

**Main Loop Frequency:** ~10ms (100Hz)  
**Weight Check:** Every 30 minutes  
**Presence Check:** Every 10 seconds  
**Telemetry:** Every 30 seconds  
**Sleep Hours:** 11 PM - 10 AM  
**Daily Goal:** 2000ml  
**Drink Threshold:** ≥30g loss  
**Refill Threshold:** ≥100g gain  
**Bottle Threshold:** <50g = removed  
**Snooze Duration:** 5 minutes  
**Max Snoozes:** 3 consecutive  
**Bottle Missing Timeout:** 3 minutes  
**Presence Fail Threshold:** 30 checks (5 min)  

---

*Last Updated: 2026-01-19*  
*System Version: 1.0*  
*Author: Babbira*
