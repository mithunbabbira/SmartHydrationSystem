#ifndef SLAVE_COMMS_H
#define SLAVE_COMMS_H

#include <WiFi.h>
#include <esp_now.h>

// --- Configuration ---
// Master MAC Address
const uint8_t MASTER_MAC[] = {0xF0, 0x24, 0xF9, 0x0D, 0x90, 0xA4};

// Protocol Commands
enum CmdType {
  CMD_SET_LED = 0x10,
  CMD_SET_BUZZER = 0x11,
  CMD_SET_RGB = 0x12,
  CMD_GET_WEIGHT = 0x20,
  CMD_REPORT_WEIGHT = 0x21
};

// Data Packet (6 Bytes)
typedef struct __attribute__((packed)) {
  uint8_t type; // 1=Hydration
  uint8_t command;
  float value;
} ControlPacket;

ControlPacket incomingPacket;
volatile bool packetReceived = false;

// Callbacks
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  // Optional: serial debug
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData,
                int len) {
  if (len == sizeof(ControlPacket)) {
    memcpy(&incomingPacket, incomingData, sizeof(ControlPacket));
    packetReceived = true;
  }
}

class SlaveComms {
public:
  void begin() {
    WiFi.mode(WIFI_STA);

    if (esp_now_init() != ESP_OK) {
      Serial.println("ESP-NOW Init Failed");
      return;
    }

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    // Register Master
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, MASTER_MAC, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add Master peer");
    }
  }

  void send(uint8_t cmd, float value) {
    ControlPacket packet;
    packet.type = 1; // Hydration
    packet.command = cmd;
    packet.value = value;

    esp_now_send(MASTER_MAC, (uint8_t *)&packet, sizeof(packet));
  }
};

#endif
