/*
 * Smart Home Slave ID 1: Hydration Monitor
 * -----------------------------------------
 * Features: Weight tracking, Alert escalation, ESP-NOW telemetry.
 *
 * Hardware: ESP32 + HX711 + Buzzer + RGB LED + Button
 */

#include "../../master_gateway/protocol.h"
#include <HX711.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// --- Pin Definitions ---
const int LOADCELL_DOUT_PIN = 23;
const int LOADCELL_SCK_PIN = 22;
const int PIN_BUZZER = 18;
const int PIN_BTN = 19;
const int PIN_RED = 5;
const int PIN_GREEN = 4;
const int PIN_BLUE = 2;

// --- Global Objects ---
HX711 scale;
Preferences prefs;
uint8_t master_mac[] = {0xFF, 0xFF, 0xFF,
                        0xFF, 0xFF, 0xFF}; // Will be broadcast or learned

// --- State ---
float current_weight = 0;
float previous_weight = 0;
float weight_delta = 0;
uint8_t alert_level = 0;
bool is_missing = false;
unsigned long last_send_time = 0;
unsigned long last_weight_check = 0;

// --- Security ---
// REPLACE THIS WITH YOUR MASTER GATEWAY MAC ADDRESS
uint8_t master_mac[6] = {0xF0, 0x24, 0xF9, 0x0D, 0x90, 0xA4}; // Gateway MAC
bool master_known = true; // Production mode enabled

// --- ESP-NOW Callbacks ---

void onDataRecv(const esp_now_recv_info *recv_info, const uint8_t *data,
                int len) {
  if (len < sizeof(ESPNowHeader))
    return;

  ESPNowHeader *header = (ESPNowHeader *)data;

  // Security Check
  if (master_known) {
    if (memcmp(recv_info->src_addr, master_mac, 6) != 0)
      return;
  } else {
    // Auto-Learn Master on first command
    if (header->slave_id == 0) {
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
  // ...

  GenericCommand *cmd = (GenericCommand *)data;

  if (cmd->command_id == 1) { // Tare
    scale.tare();
    Serial.println("✓ Tared Scale");
  } else if (cmd->command_id == 2) { // Snooze
    alert_level = 0;
    Serial.println("✓ Snoozed");
  }
}

void sendTelemetry() {
  HydrationTelemetry pkt;
  pkt.header.slave_id = SLAVE_ID_HYDRATION;
  pkt.header.msg_type = MSG_TYPE_TELEMETRY;
  pkt.header.version = PROTOCOL_VERSION;

  pkt.weight = current_weight;
  pkt.delta = weight_delta;
  pkt.alert_level = alert_level;
  pkt.bottle_missing = is_missing;

  // Use broadcast MAC unless master MAC is set (for simplicity in this demo)
  uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcast_mac, (uint8_t *)&pkt, sizeof(pkt));
}

void sendHeartbeat() {
  ESPNowHeader header;
  header.slave_id = SLAVE_ID_HYDRATION;
  header.msg_type = MSG_TYPE_TELEMETRY;
  header.version = PROTOCOL_VERSION;

  uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t *dest_mac = master_known ? master_mac : broadcast_mac;
  esp_now_send(dest_mac, (uint8_t *)&header, sizeof(header));
}

void setup() {
  Serial.begin(115200);

  // Hardware setup
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RED, OUTPUT);
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(420.0); // Calibrated value
  scale.tare();

  // WiFi & ESP-NOW
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK)
    return;

  esp_now_register_recv_cb(onDataRecv);

  // Register Master Peer if hardcoded
  esp_now_peer_info_t peerInfo = {};
  if (master_known) {
    memcpy(peerInfo.peer_addr, master_mac, 6);
  } else {
    memset(peerInfo.peer_addr, 0xFF, 6); // Broadcast peer
  }
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("Hydration Slave Ready (ESP-NOW)");
}

void loop() {
  unsigned long now = millis();

  // Weight Sensing
  if (now - last_weight_check > 500) {
    last_weight_check = now;
    current_weight = scale.get_units(5);

    // Simple presence logic
    if (current_weight < -50) { // Off balance / missing
      is_missing = true;
    } else {
      is_missing = false;
    }

    // Detect drinking
    weight_delta = current_weight - previous_weight;
    if (weight_delta < -30) {
      // User drank!
    }
    previous_weight = current_weight;
  }

  // Telemetry Update
  if (now - last_send_time > 5000) {
    last_send_time = now;
    sendTelemetry();
  }

  // Heartbeat every 10s (extra safety)
  static unsigned long last_heartbeat = 0;
  if (now - last_heartbeat > 10000) {
    last_heartbeat = now;
    sendHeartbeat();
  }
}
