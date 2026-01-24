/*
 * Smart Home Master Gateway
 * -------------------------
 * Acts as a bridge between Raspberry Pi (UART/JSON) and ESP-NOW Slaves
 * (Binary).
 *
 * Hardware: ESP32
 * Connection to Pi: GPIO 14 (TX), GPIO 15 (RX) or USB Serial
 */

#include "protocol.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// Configuration
#define BAUD_RATE 115200
#define MAX_PEERS 20

// State Tracking
struct SlavePeer {
  uint8_t mac[6];
  uint8_t slave_id;
  bool active;
  unsigned long last_seen;
};

SlavePeer known_slaves[MAX_PEERS];

// --- ESP-NOW Callbacks ---

void registerPeer(const uint8_t *mac, uint8_t slave_id) {
  // Check if exists
  for (int i = 0; i < MAX_PEERS; i++) {
    if (known_slaves[i].active && memcmp(known_slaves[i].mac, mac, 6) == 0) {
      known_slaves[i].last_seen = millis();
      return;
    }
  }

  // Add new
  for (int i = 0; i < MAX_PEERS; i++) {
    if (!known_slaves[i].active) {
      memcpy(known_slaves[i].mac, mac, 6);
      known_slaves[i].slave_id = slave_id;
      known_slaves[i].active = true;
      known_slaves[i].last_seen = millis();

      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, mac, 6);
      peerInfo.channel = 0;
      peerInfo.encrypt = false;
      esp_now_add_peer(&peerInfo);

      Serial.print("{\"event\":\"peer_added\",\"id\":");
      Serial.print(slave_id);
      Serial.println("}");
      break;
    }
  }
}

void onDataRecv(const esp_now_recv_info *recv_info, const uint8_t *data,
                int len) {
  if (len < sizeof(ESPNowHeader))
    return;

  ESPNowHeader *header = (ESPNowHeader *)data;
  registerPeer(recv_info->src_addr, header->slave_id);

  // Forward to Pi as JSON
  StaticJsonDocument<256> doc;
  doc["src"] = header->slave_id;

  if (header->msg_type == MSG_TYPE_TELEMETRY) {
    doc["type"] = "telemetry";
    JsonObject payload = doc.createNestedObject("data");

    if (header->slave_id == SLAVE_ID_HYDRATION &&
        len >= sizeof(HydrationTelemetry)) {
      HydrationTelemetry *ht = (HydrationTelemetry *)data;
      payload["weight"] = ht->weight;
      payload["delta"] = ht->delta;
      payload["alert"] = ht->alert_level;
      payload["missing"] = ht->bottle_missing;
    } else if (header->slave_id == SLAVE_ID_LED && len >= sizeof(LEDData)) {
      LEDData *ld = (LEDData *)data;
      payload["on"] = ld->is_on;
      payload["mode"] = ld->mode;
      payload["speed"] = ld->speed;
    }
  }

  serializeJson(doc, Serial);
  Serial.println();
}

void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  // Optional: Log failures to Pi
  if (status != ESP_NOW_SEND_SUCCESS) {
    // Serial.println("{\"event\":\"send_fail\"}");
  }
}

// --- Serial Command Handling ---

void handlePiCommand(String input) {
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, input);

  if (error)
    return;

  uint8_t dst = doc["dst"];
  String cmd = doc["cmd"];

  // Find Mac for dst
  uint8_t *target_mac = nullptr;
  for (int i = 0; i < MAX_PEERS; i++) {
    if (known_slaves[i].active && known_slaves[i].slave_id == dst) {
      target_mac = known_slaves[i].mac;
      break;
    }
  }

  if (!target_mac) {
    Serial.println(
        "{\"error\":\"Slave not found in peer list. Wait for heartbeat.\"}");
    return;
  }

  // Construct binary packet based on command
  if (dst == SLAVE_ID_HYDRATION) {
    GenericCommand packet;
    packet.header.slave_id = 0; // Master
    packet.header.msg_type = MSG_TYPE_COMMAND;
    packet.header.version = PROTOCOL_VERSION;

    if (cmd == "tare")
      packet.command_id = 1;
    else if (cmd == "snooze") {
      packet.command_id = 2;
      packet.val = doc["val"] | 15;
    } else if (cmd == "reset")
      packet.command_id = 3;

    esp_now_send(target_mac, (uint8_t *)&packet, sizeof(packet));
  } else if (dst == SLAVE_ID_LED) {
    LEDData packet;
    packet.header.slave_id = 0;
    packet.header.msg_type = MSG_TYPE_COMMAND;
    packet.header.version = PROTOCOL_VERSION;

    if (cmd == "set_state") {
      packet.is_on = doc["on"] | true;
      packet.mode = doc["mode"] | 0;
      packet.speed = doc["speed"] | 50;
      esp_now_send(target_mac, (uint8_t *)&packet, sizeof(packet));
    }
  } else if (dst == SLAVE_ID_IR) {
    IRData packet;
    packet.header.slave_id = 0;
    packet.header.msg_type = MSG_TYPE_COMMAND;
    packet.header.version = PROTOCOL_VERSION;
    packet.ir_code = strtoul(doc["val"], NULL, 16);
    packet.bits = 32;
    esp_now_send(target_mac, (uint8_t *)&packet, sizeof(packet));
  }
}

void setup() {
  Serial.begin(BAUD_RATE);

  // Initialize WiFi in STA Mode for ESP-NOW and force Channel 1
  WiFi.mode(WIFI_STA);

  // Print MAC Address for User Configuration (Read from eFuse)
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  Serial.printf("Gateway MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1],
                mac[2], mac[3], mac[4], mac[5]);

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("{\"event\":\"error\",\"msg\":\"esp_now_init_failed\"}");
    return;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  memset(known_slaves, 0, sizeof(known_slaves));

  Serial.println("{\"type\":\"gateway_id\",\"msg\":\"gateway_active\"}");
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    handlePiCommand(line);
  }

  // Periodic identity ping every 5 seconds
  static unsigned long last_id_ping = 0;
  if (millis() - last_id_ping > 5000) {
    last_id_ping = millis();
    Serial.println("{\"type\":\"gateway_id\",\"msg\":\"gateway_active\"}");
  }
}
