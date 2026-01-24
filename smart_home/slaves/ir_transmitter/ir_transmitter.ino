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

void setup() {
  Serial.begin(115200);
  irSender.begin();

  WiFi.mode(WIFI_STA);
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

void loop() { delay(1000); }
