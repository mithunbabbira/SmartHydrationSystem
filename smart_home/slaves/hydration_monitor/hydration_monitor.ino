/*
 * Smart Home Slave ID 1: Hydration Monitor
 * -----------------------------------------
 * Features: Weight tracking, Alert escalation, ESP-NOW telemetry.
 * Hardware: ESP32 + HX711 + Buzzer + RGB LED + Button
 */

#include "../../master_gateway/protocol.h"
#include "config.h"
#include <HX711.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// --- Global Objects ---
HX711 scale;
Preferences prefs;

// --- State ---
float current_weight = 0;
float previous_weight = 0;
float weight_delta = 0;
uint8_t alert_level = 0;
bool is_missing = false;
unsigned long last_send_time = 0;
unsigned long last_weight_check = 0;

// --- Security ---
uint8_t master_mac[6];
bool master_known = false;

// --- Forward Declarations ---
void sendHeartbeat();
void sendTelemetry();

// --- ESP-NOW Callback ---
void onDataRecv(const esp_now_recv_info *recv_info, const uint8_t *data,
                int len) {
  if (len < sizeof(ESPNowHeader))
    return;

  ESPNowHeader *header = (ESPNowHeader *)data;

  // Security Check: Verify Sender
  if (master_known) {
    if (memcmp(recv_info->src_addr, master_mac, 6) != 0)
      return;
  } else {
    // Auto-Learn Master on first command
    if (header->slave_id == 0) { // 0 is Master
      memcpy(master_mac, recv_info->src_addr, 6);
      master_known = true;

      // Add Master as Peer for Unicast
      if (!esp_now_is_peer_exist(master_mac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, master_mac, 6);
        peerInfo.channel = 1;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
      }
    }
  }

  if (header->msg_type != MSG_TYPE_COMMAND)
    return;

  GenericCommand *cmd = (GenericCommand *)data;

  if (cmd->command_id == 1) { // Tare
    scale.tare();
    Serial.println("✓ Tared Scale");
  } else if (cmd->command_id == 2) { // Snooze
    alert_level = 0;
    Serial.println("✓ Snoozed");
  } else if (cmd->command_id == 3) { // Set Alert
    alert_level = (int)cmd->val;
    Serial.printf("! Alert Level Set to: %d\n", alert_level);
    // Immediate feedback
    digitalWrite(PIN_BUZZER, HIGH);
    delay(100);
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// --- Helper Functions ---

void sendTelemetry() {
  HydrationTelemetry pkt;
  pkt.header.slave_id = SLAVE_ID_HYDRATION;
  pkt.header.msg_type = MSG_TYPE_TELEMETRY;
  pkt.header.version = PROTOCOL_VERSION;

  pkt.weight = current_weight;
  pkt.delta = weight_delta;
  pkt.alert_level = alert_level;
  pkt.bottle_missing = is_missing;

  // Send to Master (Unicast) or Broadcast if unknown
  uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t *dest = master_known ? master_mac : broadcast_mac;

  esp_now_send(dest, (uint8_t *)&pkt, sizeof(pkt));
}

void sendHeartbeat() {
  ESPNowHeader header;
  header.slave_id = SLAVE_ID_HYDRATION;
  header.msg_type = MSG_TYPE_TELEMETRY;
  header.version = PROTOCOL_VERSION;

  uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t *dest = master_known ? master_mac : broadcast_mac;
  esp_now_send(dest, (uint8_t *)&header, sizeof(header));
}

void handleAlerts() {
  if (alert_level == 0) {
    digitalWrite(PIN_RED, LOW);
    digitalWrite(PIN_GREEN, LOW);
    digitalWrite(PIN_BLUE, LOW);
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }

  static unsigned long last_blink = 0;
  unsigned long now = millis();

  // Interval: Lvl 1 = 1000ms, Lvl 2 = 300ms
  int interval =
      (alert_level == 1) ? ALERT_BLINK_WARNING_MS : ALERT_BLINK_CRITICAL_MS;

  if (now - last_blink > interval) {
    last_blink = now;
    static bool state = false;
    state = !state;

    // Visual Alert
    if (alert_level == 1) {
      digitalWrite(PIN_BLUE, state); // Warning
      digitalWrite(PIN_RED, LOW);
    } else {
      digitalWrite(PIN_RED, state); // Critical
      digitalWrite(PIN_BLUE, LOW);
    }

    // Audio Alert (Only beep on ON cycle for Critical)
    if (state && alert_level >= 2) {
      digitalWrite(PIN_BUZZER, HIGH);
      delay(ALERT_BEEP_DURATION_MS);
      digitalWrite(PIN_BUZZER, LOW);
    }
  }
}

// --- Setup & Loop ---

void setup() {
  Serial.begin(115200);

  // Hardware Setup
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_BLUE, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  Serial.println("Initializing Scale...");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(CALIBRATION_FACTOR);

  // Non-blocking wait (2 seconds max)
  if (scale.wait_ready_timeout(2000)) {
    scale.tare();
    Serial.println("✓ Scale calibrated.");
  } else {
    Serial.println(
        "⚠ Scale NOT ready! Check wiring. Continuing without calibration...");
  }

  // Network Setup
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  // PRODUCTION: Load Trusted Master MAC
  memcpy(master_mac, PRODUCTION_MASTER_MAC, 6);
  master_known = true;

  // Register Master Peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, master_mac, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("Hydration Slave Ready (Production Mode)");
}

void loop() {
  unsigned long now = millis();

  // 1. Weight Sensing
  if (now - last_weight_check > WEIGHT_SAMPLE_INTERVAL) {
    last_weight_check = now;
    current_weight = scale.get_units(5); // Average 5 readings

    // Presence Logic
    if (current_weight < WEIGHT_MISSING_THRESHOLD) {
      is_missing = true;
    } else {
      is_missing = false;
    }

    // Drinking Detection
    weight_delta = current_weight - previous_weight;
    if (weight_delta < DRINK_DETECTION_DELTA) {
      // User drank (Logic handled by server via telemetry)
    }
    previous_weight = current_weight;
  }

  // 2. Telemetry Reporting
  if (now - last_send_time > TELEMETRY_INTERVAL) {
    last_send_time = now;
    sendTelemetry();
  }

  // 3. Heartbeat Safety
  static unsigned long last_heartbeat = 0;
  if (now - last_heartbeat > HEARTBEAT_INTERVAL) {
    last_heartbeat = now;
    sendHeartbeat();
  }

  // 4. Alert Handling
  handleAlerts();
}
