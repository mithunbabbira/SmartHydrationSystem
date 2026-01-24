/*
 * Smart Home Slave ID 3: IR Transmitter
 * -----------------------------------------
 * Features: Sends IR codes via ESP-NOW commands.
 *
 * Hardware: ESP32-CAM
 */

#include "../../master_gateway/protocol.h"
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

const uint16_t IR_SEND_PIN = 4; // Check your ESP32-CAM mapping
IRsend irSender(IR_SEND_PIN);

void onDataRecv(const esp_now_recv_info *recv_info, const uint8_t *data,
                int len) {
  if (len < sizeof(ESPNowHeader))
    return;
  ESPNowHeader *header = (ESPNowHeader *)data;

  if (header->msg_type == MSG_TYPE_COMMAND && len >= sizeof(IRData)) {
    IRData *ird = (IRData *)data;
    Serial.printf("IR Sending: 0x%08X\n", ird->ir_code);
    irSender.sendNEC(ird->ir_code, ird->bits);
  }
}

void sendHeartbeat() {
  ESPNowHeader header;
  header.slave_id = SLAVE_ID_IR;
  header.msg_type = MSG_TYPE_TELEMETRY;
  header.version = PROTOCOL_VERSION;

  uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcast_mac, (uint8_t *)&header, sizeof(header));
}

void setup() {
  Serial.begin(115200);
  irSender.begin();

  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK)
    return;

  esp_now_register_recv_cb(onDataRecv);

  // Add broadcast peer
  esp_now_peer_info_t peerInfo = {};
  memset(peerInfo.peer_addr, 0xFF, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("IR Slave Ready (ESP32-CAM)");
}

void loop() {
  // Heartbeat every 5s for discovery
  static unsigned long last_heartbeat = 0;
  if (millis() - last_heartbeat > 5000) {
    last_heartbeat = millis();
    sendHeartbeat();
  }
  delay(100);
}
