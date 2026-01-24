/*
 * Smart Home Master Gateway
 * -------------------------
 * Role: Passthrough Gateway between Raspberry Pi (Serial) and Slaves (ESP-NOW)
 * Features:
 * - Auto-Discovery of Slaves (Heartbeats)
 * - Generic Packet Forwarding (Pi sends Hex -> Gateway sends Bytes)
 * - No hardcoded slave logic (Agnostic)
 */

#include "protocol.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define BAUD_RATE 115200
#define MAX_PEERS 20

// --- Slave Tracking ---
struct SlaveNode {
  uint8_t slave_id;
  uint8_t mac[6];
  unsigned long last_seen;
  bool active;
};

SlaveNode known_slaves[MAX_PEERS];

// --- ESP-NOW Callbacks ---

void onDataRecv(const esp_now_recv_info *recv_info, const uint8_t *data,
                int len) {
  if (len < sizeof(ESPNowHeader))
    return;

  ESPNowHeader *header = (ESPNowHeader *)data;

  // Track / Update Slave
  int idx = -1;
  for (int i = 0; i < MAX_PEERS; i++) {
    if (known_slaves[i].active &&
        known_slaves[i].slave_id == header->slave_id) {
      idx = i;
      break;
    }
  }

  // Register New Slave
  if (idx == -1) {
    for (int i = 0; i < MAX_PEERS; i++) {
      if (!known_slaves[i].active) {
        idx = i;
        known_slaves[i].active = true;
        known_slaves[i].slave_id = header->slave_id;
        memcpy(known_slaves[i].mac, recv_info->src_addr, 6);

        // Add Peer to ESP-NOW
        if (!esp_now_is_peer_exist(known_slaves[i].mac)) {
          esp_now_peer_info_t peerInfo = {};
          memcpy(peerInfo.peer_addr, known_slaves[i].mac, 6);
          peerInfo.channel = 1;
          peerInfo.encrypt = false;
          esp_now_add_peer(&peerInfo);
        }
        break;
      }
    }
  }

  if (idx != -1) {
    known_slaves[idx].last_seen = millis();
  }

  // Forward Telemetry to Pi as JSON
  if (header->msg_type == MSG_TYPE_TELEMETRY) {
    StaticJsonDocument<512> doc;
    doc["type"] = "telemetry";
    doc["src"] = header->slave_id;

    // We forward the RAW hex data of the packet for the Pi to decode?
    // Or we decode known types?
    // For now, let's keep partial decoding for known types to NOT BREAK
    // existing dashboards. Ideally, Pi should also accept raw hex telemetry.
    // But refactoring Pi decoding is out of scope for this task (User asked for
    // Master to be dumb for SENDING).

    // ... Existing decoding logic for Hydration ...
    if (header->slave_id == SLAVE_ID_HYDRATION) {
      HydrationTelemetry *pkt = (HydrationTelemetry *)data;
      doc["weight"] = pkt->weight;
      doc["delta"] = pkt->delta;
      doc["alert"] = pkt->alert_level;
      doc["missing"] = pkt->bottle_missing;
    }

    serializeJson(doc, Serial);
    Serial.println();
  }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Optional: Ack back to Pi?
}

// --- Serial Processing ---

void handlePiCommand(String line) {
  StaticJsonDocument<1024> doc; // Increased size for Hes strings
  DeserializationError error = deserializeJson(doc, line);

  if (error) {
    Serial.println("{\"error\":\"json_parse_error\"}");
    return;
  }

  int dst = doc["dst"];

  // Find Target MAC
  uint8_t *target_mac = nullptr;
  for (int i = 0; i < MAX_PEERS; i++) {
    if (known_slaves[i].active && known_slaves[i].slave_id == dst) {
      target_mac = known_slaves[i].mac;
      break;
    }
  }

  if (!target_mac) {
    Serial.println("{\"error\":\"slave_not_found\"}");
    return;
  }

  // Generic RAW Forwarding
  // Pi sends: {"dst": 1, "raw": "aabbcc..."}
  const char *hexRaw = doc["raw"];

  if (hexRaw) {
    String hexData = String(hexRaw);
    int len = hexData.length();
    if (len % 2 != 0)
      return; // Invalid hex

    uint8_t *buffer = new uint8_t[len / 2];

    for (int i = 0; i < len; i += 2) {
      char c1 = hexData[i];
      char c2 = hexData[i + 1];
      uint8_t val = 0;

      auto hexCharToInt = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9')
          return c - '0';
        if (c >= 'a' && c <= 'f')
          return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
          return c - 'A' + 10;
        return 0;
      };

      val = (hexCharToInt(c1) << 4) | hexCharToInt(c2);
      buffer[i / 2] = val;
    }

    esp_now_send(target_mac, buffer, len / 2);
    delete[] buffer;
    Serial.println("{\"status\":\"sent_raw\"}");
  } else {
    // Fallback for old "cmd": "..." format if needed?
    // User requested NO HARDCODING. So we assume Pi ALWAYS sends raw now.
    Serial.println("{\"error\":\"missing_raw_payload\"}");
  }
}

void setup() {
  Serial.begin(BAUD_RATE);

  WiFi.mode(WIFI_STA);
  delay(100);

  Serial.print("Gateway MAC: ");
  Serial.println(WiFi.macAddress());

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("{\"error\":\"esp_now_init_failed\"}");
    return;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  memset(known_slaves, 0, sizeof(known_slaves));
  Serial.println("{\"type\":\"gateway_id\",\"msg\":\"gateway_ready\"}");
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    handlePiCommand(line);
  }

  // Keep-Alive / Heartbeat to Pi
  static unsigned long last_ping = 0;
  if (millis() - last_ping > 5000) {
    last_ping = millis();
    Serial.println("{\"type\":\"gateway_id\",\"msg\":\"active\"}");
  }
}
