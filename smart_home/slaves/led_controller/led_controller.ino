/*
 * Smart Home Slave ID 2: BLE LED Controller
 * -----------------------------------------
 * Features: Controls a BLE LED strip based on ESP-NOW commands.
 *
 * Hardware: ESP32
 */

#include "../../master_gateway/protocol.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <WiFi.h>
#include <esp_now.h>

// --- BLE Config ---
static BLEUUID serviceUUID("FFD5");
static BLEUUID charUUID("FFD9");
String targetAddress = "ff:ff:bc:09:a5:b9";
BLERemoteCharacteristic *pRemoteCharacteristic;
bool connected = false;

// --- State ---
LEDData current_state;
unsigned long last_send_time = 0;

// --- BLE Functions ---
bool connectToBLE() {
  BLEClient *pClient = BLEDevice::createClient();
  if (pClient->connect(BLEAddress(targetAddress.c_str()))) {
    BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService != nullptr) {
      pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
      if (pRemoteCharacteristic != nullptr) {
        connected = true;
        return true;
      }
    }
  }
  return false;
}

void sendColorToStrip(uint8_t r, uint8_t g, uint8_t b) {
  if (!connected)
    return;
  uint8_t packet[] = {0x56, r, g, b, 0x00, 0xF0, 0xAA};
  pRemoteCharacteristic->writeValue(packet, sizeof(packet));
}

// --- ESP-NOW Callbacks ---

void onDataRecv(const esp_now_recv_info *recv_info, const uint8_t *data,
                int len) {
  if (len < sizeof(ESPNowHeader))
    return;
  ESPNowHeader *header = (ESPNowHeader *)data;

  if (header->slave_id == 0 && header->msg_type == MSG_TYPE_COMMAND) {
    LEDData *ld = (LEDData *)data;
    current_state = *ld;

    if (connected) {
      sendColorToStrip(ld->r, ld->g, ld->b);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // BLE Init
  BLEDevice::init("SmartHome-LED-Bridge");

  // WiFi & ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK)
    return;

  esp_now_register_recv_cb(onDataRecv);

  // Add broadcast peer (Master)
  esp_now_peer_info_t peerInfo = {};
  memset(peerInfo.peer_addr, 0xFF, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("LED Slave Ready");
}

void loop() {
  if (!connected) {
    connectToBLE();
  }
  delay(1000);
}
