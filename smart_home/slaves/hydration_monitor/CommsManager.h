#ifndef COMMS_MANAGER_H
#define COMMS_MANAGER_H

#include "../../master_gateway/protocol.h"
#include "config.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

class CommsManager {
public:
  uint8_t master_mac[6];
  bool master_known = false;

  void begin() {
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
      Serial.println("Error initializing ESP-NOW");
      return;
    }

    // Production Mode: Load from Config
    memcpy(master_mac, PRODUCTION_MASTER_MAC, 6);
    master_known = true;
    addPeer(master_mac);
    Serial.println("CommsManager: Master Configured.");
  }

  void sendTelemetry(float weight, float delta, uint8_t alert_level,
                     bool missing) {
    if (!master_known)
      return;

    HydrationTelemetry pkt;
    pkt.header.slave_id = SLAVE_ID_HYDRATION;
    pkt.header.msg_type = MSG_TYPE_TELEMETRY;
    pkt.header.version = PROTOCOL_VERSION;

    pkt.weight = weight;
    pkt.delta = delta;
    pkt.alert_level = alert_level;
    pkt.bottle_missing = missing;

    esp_now_send(master_mac, (uint8_t *)&pkt, sizeof(pkt));
  }

  void sendHeartbeat() {
    ESPNowHeader header;
    header.slave_id = SLAVE_ID_HYDRATION;
    header.msg_type = MSG_TYPE_TELEMETRY; // Keep-alive
    header.version = PROTOCOL_VERSION;

    uint8_t broadcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t *dest = master_known ? master_mac : broadcast;
    esp_now_send(dest, (uint8_t *)&header, sizeof(header));
  }

private:
  void addPeer(const uint8_t *mac) {
    if (!esp_now_is_peer_exist(mac)) {
      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, mac, 6);
      peerInfo.channel = 1;
      peerInfo.encrypt = false;
      esp_now_add_peer(&peerInfo);
    }
  }
};

#endif
